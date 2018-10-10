#include "Import/Eclipse_mesh.hpp"
#include "Utils/StringUtils.hpp"
#include "Elements/ElementFactory.hpp"
#include "Elements/Element.hpp"
#include <map>
#include "Parallel/Communicator.hpp"
#include "Utils/File.hpp"
#include "Utils/Utils.hpp"
#include "Adjacency/Adjacency.hpp"
#include <algorithm>    // std::sort

namespace PAMELA
{

	std::string Eclipse_mesh::m_label;
	unsigned int Eclipse_mesh::m_nvertices = 0;
	unsigned int Eclipse_mesh::m_nquadrilaterals = 0;
	unsigned int Eclipse_mesh::m_nhexahedra = 0;

	unsigned int Eclipse_mesh::m_nCOORD = 0;
	unsigned int Eclipse_mesh::m_nZCORN = 0;
	unsigned int Eclipse_mesh::m_nActiveCells = 0;
	unsigned int Eclipse_mesh::m_nTotalCells = 0;
	unsigned int Eclipse_mesh::m_nNNCs = 0;
	std::vector<unsigned int> Eclipse_mesh::m_SPECGRID = { 0,0,0 };
	std::vector<double>  Eclipse_mesh::m_COORD = {};
	std::vector<double>  Eclipse_mesh::m_ZCORN = {};
	std::vector<int>  Eclipse_mesh::m_ACTNUM = {};
	std::vector<Eclipse_mesh::TPFA>  Eclipse_mesh::m_NNCs = {};
	std::vector<Eclipse_mesh::TPFA>  Eclipse_mesh::m_EclipseGeneratedTrans = {};
	std::vector<double> Eclipse_mesh::m_Duplicate_Element;
	std::unordered_map<Eclipse_mesh::IJK, unsigned int, Eclipse_mesh::IJKHash> Eclipse_mesh::m_IJK2Index;
	std::unordered_map<unsigned int, Eclipse_mesh::IJK> Eclipse_mesh::m_Index2IJK;
	std::unordered_map<unsigned int, unsigned int> Eclipse_mesh::m_IndexTotal2Active;
	std::unordered_map<ECLIPSE_MESH_TYPE, ELEMENTS::TYPE> Eclipse_mesh::m_TypeMap;

	bool Eclipse_mesh::m_INIT_file=false;
	bool Eclipse_mesh::m_UNRST_file=false;

	int Eclipse_mesh::m_firstSEQ=-1;

	UNITS  Eclipse_mesh::m_UnitSystem = UNITS::UNKNOWN;

	std::unordered_map<std::string, std::vector<double>> Eclipse_mesh::m_CellProperties_double;
	std::unordered_map<std::string, std::vector<int>> Eclipse_mesh::m_CellProperties_integer;
	std::unordered_map<std::string, std::vector<double>> Eclipse_mesh::m_OtherProperties_double;
	std::unordered_map<std::string, std::vector<int>> Eclipse_mesh::m_OtherProperties_integer;
	std::unordered_map<std::string, std::vector<char>> Eclipse_mesh::m_OtherProperties_char;

	unsigned int Eclipse_mesh::m_nWells=0;
	std::unordered_map<std::string, Eclipse_mesh::WELL*> Eclipse_mesh::m_Wells;

	void Eclipse_mesh::InitElementsMapping()
	{
		m_TypeMap[ECLIPSE_MESH_TYPE::EDGE] = ELEMENTS::TYPE::VTK_LINE;
		m_TypeMap[ECLIPSE_MESH_TYPE::HEXAHEDRON] = ELEMENTS::TYPE::VTK_HEXAHEDRON;
		m_TypeMap[ECLIPSE_MESH_TYPE::QUADRILATERAL] = ELEMENTS::TYPE::VTK_QUAD;
		m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX] = ELEMENTS::TYPE::VTK_VERTEX;
	}

	Mesh* Eclipse_mesh::CreateMeshFromGRDECL(File file)
	{

		//Init map
		InitElementsMapping();

		//MPI
		auto irank = Communicator::worldRank();

		LOGINFO("*** Importing Eclipse mesh format file " + file.getNameWithoutExtension());

		std::ifstream mesh_file_;
		std::string file_content("A");

		std::vector<std::string> file_list;
		file_list.push_back(file.getFullName());
		int nfiles = 1;

		if (irank == 0)
		{
			//Open Main file
			mesh_file_.open(file.getFullName());
			ASSERT(mesh_file_.is_open(), file.getFullName() + " Could not be open");

			//Seek for include files
			std::istringstream mesh_file;
			file_content = { std::istreambuf_iterator<char>(mesh_file_), std::istreambuf_iterator<char>() };
			mesh_file_.close();
			mesh_file.str(file_content);
			std::string line, buffer;
			while (StringUtils::safeGetline(mesh_file, line))
			{
				StringUtils::RemoveStringAndFollowingContentFromLine("--", line);
				StringUtils::RemoveExtraSpaces(line);
				StringUtils::RemoveEndOfLine(line);
				StringUtils::RemoveTab(line);
				StringUtils::Trim(line);
				if (line == "INCLUDE")
				{
					mesh_file >> buffer;
					LOGINFO("---- Found Include file " + buffer + " found.");
					buffer = StringUtils::RemoveString("'", buffer);
					buffer = StringUtils::RemoveString("'", buffer);
					buffer = StringUtils::RemoveString("/", buffer);
					file_list.push_back(file.getDirectory() + "/" + buffer);
					nfiles++;
				}
			}
			mesh_file_.close();

		}

#ifdef WITH_MPI
		//Broadcast the mesh input (String)
		MPI_Bcast(&nfiles, 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
#endif

		for (auto ifile = 0; ifile != nfiles; ++ifile)
		{

			std::string file_name;
#ifdef WITH_MPI
			int file_length = 0;
#endif

			if (irank == 0)
			{
				file_name = file_list[ifile];
#ifdef WITH_MPI
				file_length = static_cast<int>(file_name.size());
#endif
			}

#ifdef WITH_MPI
			//Broadcast the file name (String)
			MPI_Bcast(&file_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
			file_name.resize(file_length);
			MPI_Bcast(&file_name[0], file_length, MPI_CHARACTER, 0, MPI_COMM_WORLD);
			MPI_Barrier(MPI_COMM_WORLD);
#endif

			if (irank == 0)
			{

				//Parse File content
				LOGINFO("---- Parsing " + file_name);
				file_content = StringUtils::FileToString(file_name);
#ifdef WITH_MPI
				file_length = static_cast<int>(file_content.size());
#endif
			}

#ifdef WITH_MPI
			//Broadcast the file content (String)
			MPI_Bcast(&file_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
			file_content.resize(file_length);
			MPI_Bcast(&file_content[0], file_length, MPI_CHARACTER, 0, MPI_COMM_WORLD);
			MPI_Barrier(MPI_COMM_WORLD);
#endif

			ParseStringFromGRDECL(file_content);

		}

		//Convert mesh into internal format
		LOGINFO("*** Converting into internal mesh format");
		auto mesh = ConvertMesh();

		//Fill mesh with imported properties
		LOGINFO("*** Filling mesh with imported properties");
		FillMeshWithProperties(mesh);

		return mesh;

	}

	std::string Eclipse_mesh::ConvertFiletoString(File file)
	{
		std::string file_content("A");
		std::string file_name("N/A");
#ifdef WITH_MPI
		int file_length = 0;
#endif

		if (Communicator::worldRank() == 0)
		{
			file_name = file.getFullName();
#ifdef WITH_MPI
			file_length = static_cast<int>(file_name.size());
#endif
			std::ifstream file_stream(file_name, std::ios::binary);
			std::istreambuf_iterator<char> b(file_stream), e;
			file_content = std::string(b, e);
			file_stream.close();
		}

#ifdef WITH_MPI
		//Broadcast the file name (String)
		MPI_Bcast(&file_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
		file_name.resize(file_length);
		MPI_Bcast(&file_name[0], file_length, MPI_CHARACTER, 0, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
#endif

		if (Communicator::worldRank() == 0)
		{
			//Parse File content
			LOGINFO("---- Parsing " + file_name);
#ifdef WITH_MPI
			file_length = static_cast<int>(file_content.size());
#endif
		}

#ifdef WITH_MPI
		//Broadcast the file content (String)
		MPI_Bcast(&file_length, 1, MPI_INT, 0, MPI_COMM_WORLD);
		file_content.resize(file_length);
		MPI_Bcast(&file_content[0], file_length, MPI_CHARACTER, 0, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
#endif

		return file_content;
	}

	Mesh* Eclipse_mesh::CreateMeshFromEclipseBinaryFiles(File egrid_file)
	{

		//Init map
		InitElementsMapping();

		//MPI
		auto irank = Communicator::worldRank();

		LOGINFO("*** Importing Eclipse mesh format file " + egrid_file.getNameWithoutExtension());

		std::vector<std::string> file_list;
		file_list.push_back(egrid_file.getFullName());
		int nfiles = 1;


		//EGRID file


		////Search for INIT file
		File init_file = File(egrid_file.getDirectory() + "/" + egrid_file.getNameWithoutExtension() + ".INIT");
		if (init_file.exists())
		{
			m_INIT_file = true;
			nfiles++;
		}


		////Search for UNRST file
		File restart_file = File(egrid_file.getDirectory() + "/" + egrid_file.getNameWithoutExtension() + ".UNRST");
		if (restart_file.exists())
		{
			m_UNRST_file = true;
			nfiles++;
		}


		if (irank == 0)
		{

			if (!egrid_file.exists())
			{
				LOGERROR(egrid_file.getFullName() + " does not exist");
			}

		}


#ifdef WITH_MPI
		//Broadcast the mesh input (String)
		MPI_Bcast(&nfiles, 1, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Barrier(MPI_COMM_WORLD);
#endif

		//EGRID
		LOGINFO("*** Parsing EGRID file");
		std::string file_content = ConvertFiletoString(egrid_file);
		ParseStringFromBinaryFile(file_content);

		//INIT
		if (m_INIT_file)
		{
			LOGINFO("*** Parsing INIT file");
			file_content.clear();
			file_content = ConvertFiletoString(init_file);
			ParseStringFromBinaryFile(file_content);
		}
		else
		{
			LOGINFO("*** INIT file not found");
		}
		

		//UNRST
		if (m_UNRST_file)
		{
			LOGINFO("*** Parsing RESTART file");
			file_content.clear();
			file_content = ConvertFiletoString(restart_file);
			ParseStringFromBinaryFile(file_content);
		}
		else
		{
			LOGINFO("*** RESTART file not found");
		}

		
		//Convert mesh into internal format
		LOGINFO("*** Converting content into internal mesh format");
		auto mesh = ConvertMesh();

		//Create transmissibility from egrid/init
		LOGINFO("*** Generating TPFA data structure from imported content");
		CreateEclipseGeneratedTrans();

		//Fill mesh with imported properties
		LOGINFO("*** Filling mesh with imported properties");
		FillMeshWithProperties(mesh);

		//Create adjacency from TPFA data
		LOGINFO("*** Generating TPFA adjacency graph");
		CreateAdjacencyFromTPFAdata("PreProc", m_EclipseGeneratedTrans, mesh);

		//Create NNC adjacency
		LOGINFO("*** Generating NNCs adjacency graph");
		CreateAdjacencyFromTPFAdata("NNCs", m_NNCs, mesh);

		//Wells
		LOGINFO("*** Generating Well & Completion data");
		CreateWellAndCompletion(mesh);

		return mesh;

	}

	Mesh* Eclipse_mesh::ConvertMesh()
	{
		Mesh* mesh = new UnstructuredMesh();
		std::vector<Point*> vertexTemp = { nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr };

		mesh->get_PolyhedronCollection()->addAndCreateGroup("POLYHEDRON_GROUP");

		int jb = 0, kb = 0, ib = 0;
		int i0 = 0;
		int np;
		int nx = m_SPECGRID[0], ny = m_SPECGRID[1], nz = m_SPECGRID[2];
		double slope;

		int i_xm, i_xp;

		std::vector<double> z_pos(8, 0), y_pos(8, 0), x_pos(8, 0);


		std::vector<int> layer;
		std::vector<unsigned int> actnum;
		std::vector<double> duplicate_polyhedron;
		layer.reserve(nx*ny*nz);
		actnum.reserve(nx*ny*nz);
		m_Duplicate_Element.reserve(nx*ny*nz);

		if (m_ACTNUM.size() == 0)
		{
			m_nActiveCells = m_nTotalCells;
			m_ACTNUM = std::vector<int>(m_nTotalCells, 1);
		}

		int icellTotal = 0;
		int icell = 0;
		int cpt = 0;
		int ipoint = 0;
		for (auto k = 0; k != nz; ++k)
		{
			for (auto j = 0; j != ny; ++j)
			{

				for (auto i = 0; i != nx; ++i)
				{
					i_xm = 4 * nx*j + i * 2 + 8 * nx*ny*k;
					i_xp = 4 * nx*j + 2 * nx + i * 2 + 8 * nx*ny*k;

					//z
					z_pos[0] = m_ZCORN[i_xm];
					z_pos[1] = m_ZCORN[i_xm + 1];
					z_pos[2] = m_ZCORN[i_xp];
					z_pos[3] = m_ZCORN[i_xp + 1];

					z_pos[4] = m_ZCORN[i_xm + 4 * nx*ny];
					z_pos[5] = m_ZCORN[i_xm + 1 + 4 * nx*ny];
					z_pos[6] = m_ZCORN[i_xp + 4 * nx*ny];
					z_pos[7] = m_ZCORN[i_xp + 1 + 4 * nx*ny];


					////Pillar 1
					np = i + (nx + 1)*j;

					//1
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[0] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[0] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[0] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					//5
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[4] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[4] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[4] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					////Pillar 2
					np = i + 1 + (nx + 1)*j;
					//2
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[1] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[1] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[1] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					//6
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[5] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[5] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[5] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];



					////Pillar 3
					np = i + (nx + 1)*(j + 1);
					//3
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[2] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[2] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[2] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					//7
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[6] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[6] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[6] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					////Pillar 4
					np = i + (nx + 1)*(j + 1) + 1;
					//4
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[3] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[3] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[3] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];


					//8
					i0 = np * 6 - 1;
					if (!utils::nearlyEqual(m_COORD[i0 + 6] - m_COORD[i0 + 3], 0.))
					{
						slope = (z_pos[7] - m_COORD[i0 + 3]) / (m_COORD[i0 + 6] - m_COORD[i0 + 3]);
					}
					else
					{
						slope = 1;
					}
					x_pos[7] = slope * (m_COORD[i0 + 4] - m_COORD[i0 + 1]) + m_COORD[i0 + 1];
					y_pos[7] = slope * (m_COORD[i0 + 5] - m_COORD[i0 + 2]) + m_COORD[i0 + 2];



					if (m_ACTNUM[icellTotal] == 1)
					{

						m_IndexTotal2Active[icellTotal] = icell;

						//Points
						vertexTemp[0] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 7, "POINT_GROUP_0", x_pos[0], y_pos[0], z_pos[0]);
						vertexTemp[4] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 6, "POINT_GROUP_0", x_pos[4], y_pos[4], z_pos[4]);
						vertexTemp[1] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 5, "POINT_GROUP_0", x_pos[1], y_pos[1], z_pos[1]);
						vertexTemp[5] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 4, "POINT_GROUP_0", x_pos[5], y_pos[5], z_pos[5]);
						vertexTemp[3] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 3, "POINT_GROUP_0", x_pos[2], y_pos[2], z_pos[2]);
						vertexTemp[7] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 2, "POINT_GROUP_0", x_pos[6], y_pos[6], z_pos[6]);
						vertexTemp[2] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 1, "POINT_GROUP_0", x_pos[3], y_pos[3], z_pos[3]);
						vertexTemp[6] = mesh->addPoint(m_TypeMap[ECLIPSE_MESH_TYPE::VERTEX], ipoint - 0, "POINT_GROUP_0", x_pos[7], y_pos[7], z_pos[7]);

						//Hexa
						mesh->get_PolyhedronCollection()->MakeActiveGroup("POLYHEDRON_GROUP");
						auto returned_element = mesh->addPolyhedron(m_TypeMap[ECLIPSE_MESH_TYPE::HEXAHEDRON], icell, "POLYHEDRON_GROUP", vertexTemp);

						if (returned_element != nullptr)
						{
							//m_Duplicate_Element[returned_element->get_globalIndex()] = 1;
							m_Duplicate_Element.push_back(2);
							cpt++;
						}
						else
						{
							m_Duplicate_Element.push_back(0);
						}

						m_IJK2Index[IJK(i,j,k)] = icell;
						m_Index2IJK[icell] = IJK(i, j, k);
						icell++;
					}

					layer.push_back(k);
					actnum.push_back(m_ACTNUM[icellTotal]);
					icellTotal++;
				}
			}
		}

		layer.shrink_to_fit();
		actnum.shrink_to_fit();
		m_Duplicate_Element.shrink_to_fit();

		//Layers
		m_CellProperties_integer["Layer"] = layer;

		//Remove non-active properties
		auto iacthexas = icell;

		std::vector<double> temp_double;
		for (auto it = m_CellProperties_double.begin(); it != m_CellProperties_double.end(); ++it)
		{

			if ((it->second.size()) == m_nTotalCells)	//Eliminate values on inactive blocks
			{

				temp_double.clear();
				temp_double.reserve(m_nActiveCells);
				auto& prop = it->second;
				for (size_t i = 0; i != prop.size(); ++i)
				{
					if (actnum[i] == 1)
					{

						temp_double.push_back(prop[i]);
					}
				}

				prop = temp_double;
			}

		}

		//Inactive blocks
		//--Delete properties on inactive blocks
		std::vector<int> temp_int;
		for (auto it = m_CellProperties_integer.begin(); it != m_CellProperties_integer.end(); ++it)
		{
			if ((it->second.size()) == m_nTotalCells)	//Eliminate values on inactive blocks
			{
				temp_int.clear();
				temp_int.reserve(m_nActiveCells);
				auto& prop = it->second;
				for (size_t i = 0; i != prop.size(); ++i)
				{
					if (actnum[i] == 1)
					{
						temp_int.push_back(prop[i]);
					}
				}
				prop = temp_int;
			}
		}

		//--Delete NNC on inactive blocks
		temp_int.clear();
		
		for(unsigned int i=0;i!=m_NNCs.size();++i)
		{
			if (m_ACTNUM[m_NNCs[i].downstream_index]==1&& m_ACTNUM[m_NNCs[i].upstream_index]==1)
			{
				temp_int.push_back(i);
			}
		}
		std::vector <TPFA> temp_NNC; temp_NNC.reserve(temp_int.size());
		for (unsigned int i = 0; i != temp_int.size(); ++i)
		{
			temp_NNC.push_back(m_NNCs[temp_int[i]]);
		}
		m_NNCs.clear();
		m_NNCs = temp_NNC;
		for (auto it = m_NNCs.begin(); it != m_NNCs.end(); ++it)
		{
			it->downstream_index = m_IndexTotal2Active.at(it->downstream_index);
			it->upstream_index = m_IndexTotal2Active.at(it->upstream_index);

			

		}

		LOGINFO(std::to_string(m_nTotalCells) + "  total hexas");
		LOGINFO(std::to_string(m_nActiveCells) + "  active hexas");
		LOGINFO(std::to_string(cpt) + "  duplicated hexas");

		return mesh;

	}

	void Eclipse_mesh::FillMeshWithProperties(Mesh* mesh)
	{
		//Temp var
		auto props_double = mesh->get_PolyhedronProperty_double();
		auto props_int = mesh->get_PolyhedronProperty_int();

		//Transfer property to mesh object
		for (auto it = m_CellProperties_double.begin(); it != m_CellProperties_double.end(); ++it)
		{
			ASSERT(it->second.size() == props_double->get_Owner()->size_owned(), "Property set size is different from its owner");
			props_double->ReferenceProperty(it->first);
			props_double->SetProperty(it->first, it->second);
		}

		for (auto it = m_CellProperties_integer.begin(); it != m_CellProperties_integer.end(); ++it)
		{
			ASSERT(it->second.size() == props_int->get_Owner()->size_owned(), "Property set size is different from its owner");
			props_int->ReferenceProperty(it->first);
			props_int->SetProperty(it->first, it->second);
		}
	}

	void Eclipse_mesh::ParseStringFromGRDECL(std::string& str)
	{
		std::istringstream mesh_file;
		mesh_file.str(str);
		std::string line, buffer;
		while (getline(mesh_file, line))
		{
			StringUtils::RemoveStringAndFollowingContentFromLine("--", line);
			StringUtils::RemoveExtraSpaces(line);
			StringUtils::RemoveEndOfLine(line);
			StringUtils::RemoveTab(line);
			StringUtils::Trim(line);
			if (line == "SPECGRID")
			{
				LOGINFO("     o SPECGRID Found");
				std::vector<int> buf_int;
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::FromStringTo(buffer, buf_int);
				m_SPECGRID[0] = buf_int[0];
				m_SPECGRID[1] = buf_int[1];
				m_SPECGRID[2] = buf_int[2];
				m_nTotalCells = m_SPECGRID[0] * m_SPECGRID[1] * m_SPECGRID[2];
				m_nCOORD = 6 * (m_SPECGRID[1] + 1) * 6 * (m_SPECGRID[0] + 1);
				m_nZCORN = 8 * m_SPECGRID[0] * m_SPECGRID[1] * m_SPECGRID[2];
				m_ZCORN.reserve(m_nZCORN);
				m_COORD.reserve(m_nCOORD);

			}
			else if (line == "COORD")
			{
				LOGINFO("     o COORD Found");
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_COORD);
			}
			else if (line == "ZCORN")
			{
				LOGINFO("     o ZCORN Found");
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::FromStringTo(buffer, m_ZCORN);
			}
			else if (line == "ACTNUM")
			{
				LOGINFO("     o ACTNUM Found");
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_ACTNUM);
				std::replace(m_ACTNUM.begin(), m_ACTNUM.end(), 2, 0);
				std::replace(m_ACTNUM.begin(), m_ACTNUM.end(), 3, 0);
				m_nActiveCells = std::accumulate(m_ACTNUM.begin(), m_ACTNUM.end(), 0);
			}
			else if (line == "NNC")
			{
				LOGINFO("     o NNC Found");
				buffer = extractDataBelowKeyword(mesh_file);
			}
			else if (line == "PORO")
			{
				LOGINFO("     o PORO Found");
				m_CellProperties_double["PORO"].reserve(m_nTotalCells);
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_CellProperties_double["PORO"]);
			}
			else if (line == "PERMX")
			{
				LOGINFO("     o PERMX Found");
				m_CellProperties_double["PERMX"].reserve(m_nTotalCells);
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_CellProperties_double["PERMX"]);
			}
			else if (line == "PERMY")
			{
				LOGINFO("     o PERMY Found");
				m_CellProperties_double["PERMY"].reserve(m_nTotalCells);
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_CellProperties_double["PERMY"]);
			}
			else if (line == "PERMZ")
			{
				LOGINFO("     o PERMZ Found");
				m_CellProperties_double["PERMZ"].reserve(m_nTotalCells);
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_CellProperties_double["PERMZ"]);
			}
			else if (line == "NTG")
			{
				LOGINFO("     o NTG Found");
				m_CellProperties_double["NTG"].reserve(m_nTotalCells);
				buffer = extractDataBelowKeyword(mesh_file);
				StringUtils::ExpandStarExpression(buffer);
				StringUtils::FromStringTo(buffer, m_CellProperties_double["NTG"]);
			}
		}

		ASSERT(m_nTotalCells != 0, "Grid dimension information missing");

	}

	void Eclipse_mesh::ParseStringFromBinaryFile(std::string& str)
	{
		std::string prefix;
		int index = 0;
		do
		{
			///////		Extract from file
			//Skip delimiter
			index = index + 4;

			//Read keyword - size 8
			std::string keyword = str.substr(index, 8);
			//strcpy_s(keyword, 8 ,str.substr(index, 8).c_str());

			index = index + 8;

			LOGINFO("Keyword " + std::string(keyword) + " found.");

			//Read dim
			int ksize = *reinterpret_cast<int *>(&str.substr(index, 4)[0]);
			utils::bites_swap(&ksize);
			index = index + 4;

			//Read type
			std::string ktype = str.substr(index, 4);
			index = index + 4;

			//Skip delimiter
			index = index + 4;

			StringUtils::RemoveExtraSpaces(keyword);
			StringUtils::RemoveEndOfLine(keyword);
			StringUtils::RemoveTab(keyword);
			StringUtils::Trim(keyword);

			if (ktype == "INTE")
			{
				int kdim = 4;
				std::vector<int> data;
				ExtractBinaryBlock(str, index, ksize, kdim, data);

				if (keyword == "SEQNUM")		//for UNRST file
				{
					prefix = "_SEQNUM_" + std::to_string(data[0]);
					if (m_firstSEQ==-1)
					{
						m_firstSEQ = data[0];
					}
				}
				else
				{
					ConvertBinaryBlock(keyword, prefix, data);
				}
				
			}
			else if (ktype == "REAL")
			{
				int kdim = 4;
				std::vector<float> data;
				ExtractBinaryBlock(str, index, ksize, kdim, data);
				std::vector<double> temp(data.begin(), data.end());
				ConvertBinaryBlock(keyword, prefix, temp);
			}
			else if (ktype == "CHAR")
			{
				int kdim = 8;
				std::vector <char> data;
				ExtractBinaryBlock(str, index, ksize, kdim, data);
				ConvertBinaryBlock(keyword, prefix, data);
			}
			else if (ktype == "LOGI")
			{
				int kdim = 4;
				std::vector <bool> data;
				ExtractBinaryBlock(str, index, ksize, kdim, data);
				ConvertBinaryBlock(keyword, prefix, data);
			}
			else if (ktype == "DOUB")
			{
				int kdim = 8;
				std::vector <double> data;
				ExtractBinaryBlock(str, index, ksize, kdim, data);
				ConvertBinaryBlock(keyword, prefix, data);
			}
			else
			{
				LOGWARNING(ktype + " EGRID type not supported");
			}

		} while (index < static_cast<int>(str.size()));

		auto jj = 4;


	}

	std::string Eclipse_mesh::extractDataBelowKeyword(std::istringstream& string_block)
	{
		char KeywordEnd = '/';
		std::string chunk;
		std::streampos pos;
		std::vector<std::string> res;
		getline(string_block, chunk, KeywordEnd);
		string_block.clear();
		StringUtils::RemoveTab(chunk);
		StringUtils::RemoveEndOfLine(chunk);
		StringUtils::Trim(chunk);
		res.push_back(chunk);
		string_block.ignore(10, '\n');
		getline(string_block, chunk);
		StringUtils::RemoveTab(chunk);
		StringUtils::RemoveEndOfLine(chunk);
		return res[0];
	}

	template<>
	void Eclipse_mesh::ConvertBinaryBlock(std::string keyword, std::string label_prefix, std::vector<double>& data)
	{
		if (keyword == "COORD")
		{
			LOGINFO("     o COORD processed");
			m_COORD = data;		//TODO not efficient
		}
		else if (keyword == "ZCORN")
		{
			LOGINFO("     o ZCORN processed");
			m_ZCORN = data;
		}
		else if (keyword == "TRANNNC")
		{
			LOGINFO("     o TRANNNC processed");
			//m_CellProperties_double[keyword] = data;
			for (unsigned int i = 0; i < m_nNNCs; ++i)
			{
				m_NNCs[i].transmissibility = data[i];
			}
		}
		else
		{
			if ((data.size() == m_nActiveCells) || (data.size() == m_nTotalCells))//|| (data.size() == m_nNNCs))
			{
				LOGINFO("     o " + keyword + " processed. Dimension is " + std::to_string(data.size()));
				m_CellProperties_double[keyword + label_prefix] = data;
			}
			else
			{
				LOGINFO("     o " + keyword + " processed. Dimension is " + std::to_string(data.size()));
				m_OtherProperties_double[keyword  + label_prefix] = data;
			}
		}

	}

	template<>
	void Eclipse_mesh::ConvertBinaryBlock(std::string keyword, std::string label_prefix, std::vector<int>& data)
	{
		if (keyword == "GRIDHEAD")
		{
			LOGINFO("     o GRIDHEAD Found");
			m_SPECGRID[0] = data[1];
			m_SPECGRID[1] = data[2];
			m_SPECGRID[2] = data[3];
			m_nTotalCells = m_SPECGRID[0] * m_SPECGRID[1] * m_SPECGRID[2];
			LOGINFO(std::to_string(m_nTotalCells) + " numbers of cells");
		}
		else if (keyword == "NNCHEAD")
		{
			LOGINFO("     o NNCHEAD Found");
			m_nNNCs = data[0];
			m_NNCs = std::vector<TPFA>(m_nNNCs);
			LOGINFO(std::to_string(m_nNNCs) + " numbers of NNC connections");
		}
		else if (keyword == "ACTNUM")
		{
			if (!m_ACTNUM.empty()) //ACTNUM already processed, now its size will be m_nActiveCells
			{
				LOGINFO("     o ACTNUM updated");
				auto i = 0;
				for (auto it = m_ACTNUM.begin(); it != m_ACTNUM.end(); ++it)
				{
					if ((*it) == 1)
					{
						*it = data[i];
						++i;
					}
				}
				m_nActiveCells = std::accumulate(m_ACTNUM.begin(), m_ACTNUM.end(), 0);
				LOGINFO(std::to_string(m_nActiveCells) + " numbers of active cells");
			}
			else //First time
			{
				LOGINFO("     o ACTNUM processed");
				m_ACTNUM = data;
				std::replace(m_ACTNUM.begin(), m_ACTNUM.end(), 2, 0);
				std::replace(m_ACTNUM.begin(), m_ACTNUM.end(), 3, 0);
				m_nActiveCells = std::accumulate(m_ACTNUM.begin(), m_ACTNUM.end(), 0);
				LOGINFO(std::to_string(m_nActiveCells) + " numbers of active cells");
			}


		}
		else if (keyword == "NNC1")
		{
			LOGINFO("     o NNC1 processed");
			for (unsigned int i=0;i<m_nNNCs;++i)
			{
				m_NNCs[i].downstream_index = data[i]-1;
			}
		}
		else if (keyword == "NNC2")
		{
			LOGINFO("     o NNC2 processed");
			for (unsigned int i = 0; i < m_nNNCs; ++i)
			{
				m_NNCs[i].upstream_index = data[i]-1;
			}
		}
		else
		{
			if ((data.size() == m_nActiveCells) || (data.size() == m_nTotalCells))//|| (data.size() == m_nNNCs))
			{
				LOGINFO("     o " + keyword + " processed. Dimension is " + std::to_string(data.size()));
				m_CellProperties_integer[keyword + label_prefix] = data;
			}
			else
			{
				LOGINFO("     o " + keyword + " processed. Dimension is " + std::to_string(data.size()));
				m_OtherProperties_integer[keyword  + label_prefix] = data;
			}
		}


	}

	template<>
	void Eclipse_mesh::ConvertBinaryBlock(std::string keyword, std::string label_prefix, std::vector<char>& data)
	{

		LOGINFO("     o " + keyword + " processed. Dimension is " + std::to_string(data.size()));
		m_OtherProperties_char[keyword + label_prefix] = data;


	}

	void Eclipse_mesh::CreateAdjacencyFromTPFAdata(std::string label, std::vector<TPFA>& data, Mesh* mesh)
	{
		auto new_adj = new Adjacency(ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::UNKNOWN, mesh->get_PolyhedronCollection(), mesh->get_PolyhedronCollection(), nullptr);

		if (!data.empty())
		{
			
			auto csr_mat = new_adj->get_adjacencySparseMatrix();
			auto nb_polyhedron = mesh->get_PolyhedronCollection()->size_all();
			csr_mat->fillEmpty(static_cast<int>(nb_polyhedron), static_cast<int>(nb_polyhedron));

			//---NNCs
			//Sort TPFANNC
			std::sort(data.begin(), data.end());

			int last_irow = 0, last_rowptr = 0;
			int cpt = 1;
			for (unsigned int i = 0; i != data.size(); ++i)
			{
				int irow = data[i].downstream_index;
				int icol = data[i].upstream_index;

				if (irow != last_irow)
				{
					std::fill(csr_mat->rowPtr.begin() + last_irow + 1, csr_mat->rowPtr.begin() + irow, last_rowptr);
					csr_mat->rowPtr[irow] = static_cast<int>(csr_mat->columnIndex.size());
					csr_mat->rowPtr[irow + 1] = static_cast<int>(csr_mat->columnIndex.size() + 1);
					last_irow = irow;
				}
				else
				{
					csr_mat->rowPtr[irow + 1]++;
				}
				last_rowptr = csr_mat->rowPtr[irow + 1];
				csr_mat->columnIndex.push_back(icol);
				csr_mat->values.push_back(1);
				csr_mat->nnz++;
			}
			std::fill(csr_mat->rowPtr.begin() + last_irow + 1, csr_mat->rowPtr.end(), last_rowptr);
			csr_mat->checkMatrix();
		}
		mesh->getAdjacencySet()->Add_NonTopologicalAdjacency(label, new_adj);

	}

	void Eclipse_mesh::CreateEclipseGeneratedTrans()
	{

		if (!m_CellProperties_double.at("TRANX").empty())
		{
			ASSERT(m_CellProperties_double.at("TRANX").size() == m_CellProperties_double.at("TRANY").size() && m_CellProperties_double.at("TRANY").size() == m_CellProperties_double.at("TRANZ").size(), "Size mismatch");

			auto ntran = static_cast<int>(m_CellProperties_double.at("TRANX").size());
			auto& tranx = m_CellProperties_double.at("TRANX");
			auto& trany = m_CellProperties_double.at("TRANY");
			auto& tranz = m_CellProperties_double.at("TRANZ");

			TPFA temp_TPFA;
			for (auto i = 0; i != ntran; ++i)
			{
				auto ijk = m_Index2IJK.at(i);

				//X
				if (tranx[i]>0)
				{
					ijk.I = ijk.I + 1;
					if (m_IJK2Index.find(ijk) != m_IJK2Index.end())
					{
						temp_TPFA.downstream_index = i;
						temp_TPFA.upstream_index = m_IJK2Index.at(ijk);
						temp_TPFA.transmissibility = tranx[i];
						m_EclipseGeneratedTrans.push_back(temp_TPFA);
					}
					ijk.I = ijk.I - 1;
				}
				
				//Y
				if (tranx[i] > 0)
				{
					ijk.J = ijk.J + 1;
					if (m_IJK2Index.find(ijk) != m_IJK2Index.end())
					{
						temp_TPFA.downstream_index = i;
						temp_TPFA.upstream_index = m_IJK2Index.at(ijk);
						temp_TPFA.transmissibility = trany[i];
						m_EclipseGeneratedTrans.push_back(temp_TPFA);
					}
					ijk.J = ijk.J - 1;
				}

				//Z
				if (tranx[i] > 0)
				{
					ijk.K = ijk.K + 1;
					if (m_IJK2Index.find(ijk) != m_IJK2Index.end())
					{
						temp_TPFA.downstream_index = i;
						temp_TPFA.upstream_index = m_IJK2Index.at(ijk);
						temp_TPFA.transmissibility = tranz[i];
						m_EclipseGeneratedTrans.push_back(temp_TPFA);
					}
					ijk.K = ijk.K - 1;
				}
			}

		}

		
	}

	void Eclipse_mesh::CreateWellAndCompletion(Mesh* mesh)
	{
		//Import well data
		if (m_OtherProperties_integer.find("INTEHEAD")!= m_OtherProperties_integer.end())
		{

			std::string seq_prefix = "_SEQNUM_" + std::to_string(m_firstSEQ);

			auto intehead = m_OtherProperties_integer.at("INTEHEAD"+ seq_prefix);
			m_nWells = intehead[16];
			auto niwelz = intehead[24];
			auto iwel = m_OtherProperties_integer.at("IWEL" + seq_prefix);
			auto scon = m_OtherProperties_double.at("SCON" + seq_prefix);
			auto icon = m_OtherProperties_integer.at("ICON" + seq_prefix);
			auto nsconz= intehead[33];
			auto niconz = intehead[32];
			auto ncwmax= intehead[17];
			for (unsigned int iw=0;iw!= m_nWells;++iw)
			{
				std::vector<int> sub_iwel(&iwel[0 + iw*niwelz], &iwel[(iw+1)*(niwelz - 1)]);
				auto well_type = sub_iwel[6];
				std::string label;
				if (well_type==1)
				{
					label = "PRODUCER";
				}
				else if (well_type == 2)
				{
					label = "OIL_INJECTOR";
				}
				else if (well_type == 3)
				{
					label = "WATER_INJECTOR";
				}
				else if (well_type == 4)
				{
					label = "GAS_INJECTOR";
				}
				label = label + "_" + std::to_string(iw);

				auto icell = m_IJK2Index.at(IJK(sub_iwel[0]-1, sub_iwel[1]-1, sub_iwel[2]-1));
				auto nb_comp = sub_iwel[4];
				auto well = m_Wells[label] = new WELL(icell,nb_comp);
				std::vector<double> sub_scon(&scon[0 + iw * ncwmax * nsconz], &scon[(iw + 1)*(ncwmax * nsconz - 1)]);
				std::vector<int> sub_icon(&icon[0 + iw * ncwmax * niconz], &icon[(iw + 1)*(ncwmax * niconz - 1)]);
				for (int ic = 0; ic != nb_comp; ++ic)
				{
					std::vector<double> sub_sub_scon(&sub_scon[0 + ic*nsconz], &sub_scon[(ic + 1)*(nsconz - 1)]);
					std::vector<int> sub_sub_icon(&sub_icon[0 + ic * niconz], &sub_icon[(ic + 1)*(niconz - 1)]);
					auto cf = sub_sub_scon[0];
					auto kh = sub_sub_scon[3];
					auto icell_comp = m_IJK2Index.at(IJK(sub_sub_icon[1] - 1, sub_sub_icon[2] - 1, sub_sub_icon[3] - 1));
					well->completions.push_back(COMPLETION(icell_comp, cf, kh));
				}

			}

		}

		//Create line and point groups
		auto polyhedron_collection = mesh->get_PolyhedronCollection();
		for (auto itw=m_Wells.begin(); itw != m_Wells.end();++itw)
		{
			std::vector<Point*> vecpoint;
			auto well = itw->second;
			auto hcindex = well->head_cell_index;
			auto itpol = polyhedron_collection->begin_owned() + hcindex;
			auto xyz = (*itpol)->get_centroidCoordinates();
			vecpoint.push_back(ElementFactory::makePoint(ELEMENTS::TYPE::VTK_VERTEX, -1, xyz[0], xyz[1], 0));
			auto comps = well->completions;
			for (unsigned int ic = 0; ic != well->nb_completions; ++ic)
			{
				auto cell_index = comps[ic].hosting_cell_index;
				auto itpol2 = polyhedron_collection->begin_owned() + cell_index;
				auto xyz2 = (*itpol2)->get_centroidCoordinates();
				vecpoint.push_back(ElementFactory::makePoint(ELEMENTS::TYPE::VTK_VERTEX, -1, xyz2[0], xyz2[1], xyz2[2]));
			}
			mesh->AddImplicitLine(ELEMENTS::TYPE::VTK_LINE, itw->first, vecpoint);
			mesh->get_ImplicitLineCollection()->MakeActiveGroup(itw->first);
		}
	}
}
