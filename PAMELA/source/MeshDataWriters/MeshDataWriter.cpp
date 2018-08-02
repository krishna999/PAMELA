#include "MeshDataWriters/MeshDataWriter.hpp"
#include "Mesh/Mesh.hpp"

namespace PAMELA
{
	MeshDataWriter::MeshDataWriter(Mesh * mesh, std::string name) : m_mesh(mesh), m_name(name), m_partition(Communicator::worldRank()), m_nPartition(Communicator::worldSize())
	{
		LOGINFO("***Mesh Data Writer Construction");

		//////Misc
		//Number of digits for file extension
		m_nDigitsExtensionPartition = 5;
		m_nDigitsExtensionTime = 5;

		//Time
		m_currentTime = 0.0;
		m_currentTimeStep = 0;

		//////Initialize Parts

		//Part Index
		int partIndex = 1;

		//------------------------------------------------------------- LineCollection -------------------------------------------------------------
		auto LineCollection = m_mesh->get_LineCollection();
		auto ActiveGroupMapLine = LineCollection->get_ActiveGroupsMap();

		//--Add active parts
		for (auto it = ActiveGroupMapLine.begin(); it != ActiveGroupMapLine.end(); ++it)	//Loop over group and act on active groups
		{
			if (it->second)
			{
				std::string grplabel = it->first;
				auto groupEnsemble = LineCollection->get_Group(grplabel);
				m_LineParts[grplabel] = new Part<Line*>(grplabel, partIndex, groupEnsemble);
				partIndex++;
			}
		}
		FillParts("PART" + PartitionNumberForExtension() + "_" + "LINE", &m_LineParts);


		//------------------------------------------------------------- PolygonCollection -------------------------------------------------------------
		auto PolygonCollection = m_mesh->get_PolygonCollection();
		auto ActiveGroupMapPolygon = PolygonCollection->get_ActiveGroupsMap();

		//--Add active parts
		for (auto it = ActiveGroupMapPolygon.begin(); it != ActiveGroupMapPolygon.end(); ++it)	//Loop over group and act on active groups
		{
			if (it->second)
			{
				std::string grplabel = it->first;
				auto groupEnsemble = PolygonCollection->get_Group(grplabel);
				m_PolygonParts[grplabel] = new Part<Polygon*>(grplabel, partIndex, groupEnsemble);
				partIndex++;
			}
		}
		FillParts("PART" + PartitionNumberForExtension() + "_" + "POLYGON", &m_PolygonParts);

		//------------------------------------------------------------- PolyhedronCollection -------------------------------------------------------------
		auto PolyhedronCollection = m_mesh->get_PolyhedronCollection();
		auto ActiveGroupMapPolyhedron = PolyhedronCollection->get_ActiveGroupsMap();

		//--Add active parts
		for (auto it = ActiveGroupMapPolyhedron.begin(); it != ActiveGroupMapPolyhedron.end(); ++it)	//Loop over group and act on active groups
		{
			if (it->second )
			{
				std::string grplabel = it->first;
				auto groupEnsemble = PolyhedronCollection->get_Group(grplabel);
				m_PolyhedronParts[grplabel] = new Part<Polyhedron*>(grplabel, partIndex, groupEnsemble);
				partIndex++;
			}
		}
		FillParts("PART" + PartitionNumberForExtension() + "_" + "POLYHEDRON", &m_PolyhedronParts);

		LOGINFO("*** Done");
			}


	void MeshDataWriter::DeclareVariable(FAMILY family, VARIABLE_TYPE dtype, VARIABLE_LOCATION dloc, std::string name, std::string part)
	{

		switch (family)
		{
		case FAMILY::POINT:
			m_Variable[VariableKey(name, part)] = m_PointParts[part]->AddVariable(dtype, dloc, name);
			break;
		case FAMILY::LINE:
			m_Variable[VariableKey(name, part)] = m_LineParts[part]->AddVariable(dtype, dloc, name);
			break;
		case FAMILY::POLYGON:
			m_Variable[VariableKey(name, part)] = m_PolygonParts[part]->AddVariable(dtype, dloc, name);
			break;
		case FAMILY::POLYHEDRON:
			m_Variable[VariableKey(name, part)] = m_PolyhedronParts[part]->AddVariable(dtype, dloc, name);
			break;
		default:;
		}

	}

	void MeshDataWriter::DeclareVariable(FAMILY family, VARIABLE_TYPE dtype, VARIABLE_LOCATION dloc, std::string name)
	{

		switch (family)
		{
		case FAMILY::POINT:
			for (auto const& part : m_PointParts)
			{
				DeclareVariable(family, dtype, dloc, name, part.first);
			}
			break;
		case FAMILY::LINE:
			for (auto const& part : m_LineParts)
			{
				DeclareVariable(family, dtype, dloc, name, part.first);
			}
			break;
		case FAMILY::POLYGON:
			for (auto const& part : m_PolygonParts)
			{
				DeclareVariable(family, dtype, dloc, name, part.first);
			}
			break;
		case FAMILY::POLYHEDRON:
			for (auto const& part : m_PolyhedronParts)
			{
				DeclareVariable(family, dtype, dloc, name, part.first);
			}
			break;
		default:;
		}


	}


	/////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	//------------------------------------------ Utils

	/**
	* \brief
	* \return
	*/
	std::string MeshDataWriter::TimeStepNumberForExtension()
	{
		std::string Ext = std::to_string(m_currentTimeStep);
		int nExt = static_cast<int>(Ext.size());
		for (auto i = 0; i < (m_nDigitsExtensionTime - nExt); ++i)
		{
			Ext.insert(0, "0");
		}

		return  Ext;
	}


	void MeshDataWriter::DeclareAdjacency(std::string label, Adjacency* adjacency)
	{

		//Reference
		m_Adjacency.emplace_back(label);
		auto& adj_data = m_Adjacency.back();

		//CSR Matrix
		auto csr_matrix = adjacency->get_adjacencySparseMatrix();
		auto dimRow = csr_matrix->dimRow;
		auto nnz = csr_matrix->nnz;
		auto columIndex = csr_matrix->columnIndex;
		auto rowPtr = csr_matrix->rowPtr;


		if ((adjacency->get_sourceFamily() == ELEMENTS::FAMILY::POLYHEDRON) && (adjacency->get_targetFamily() == ELEMENTS::FAMILY::POLYHEDRON))
		{
			auto sourcetarget = static_cast<PolyhedronCollection*>(adjacency->get_sourceElementCollection());

			//Compute Node coordinates
			int isource=0, itarget=0;
			int cpt = 0;
			for (auto irow = 0; irow != dimRow; ++irow)
			{
				auto it = m_mesh->get_PolyhedronCollection()->begin_owned() + irow;
				auto xyz = (*it)->get_centroidCoordinates();
				Point source_point = Point(isource,xyz[0], xyz[1], xyz[2]);
				adj_data.NodesVector.push_back(source_point);
				itarget = isource;
				for (auto icol = rowPtr[irow]; icol != rowPtr[irow + 1]; ++icol)
				{
					itarget=itarget+1;
					auto it = m_mesh->get_PolyhedronCollection()->begin_owned() + columIndex[icol];
					auto xyz = (*it)->get_centroidCoordinates();
					Point target_point = Point(itarget, xyz[0], xyz[1], xyz[2]);
					adj_data.NodesVector.push_back(target_point);
					adj_data.iSource.push_back(isource);
					adj_data.iTarget.push_back(itarget);
				}
				isource = itarget + 1;
				cpt++;
			}

		}

		auto pp = 4;

	}

	/**
	* \brief
	* \return
	*/
	std::string MeshDataWriter::PartitionNumberForExtension()
	{
		std::string Ext = std::to_string(m_partition + 1);
		int nExt = static_cast<int>(Ext.size());
		for (auto i = 0; i < (m_nDigitsExtensionPartition - nExt); ++i)
		{
			Ext.insert(0, "0");
		}

		return  Ext;
	}



}