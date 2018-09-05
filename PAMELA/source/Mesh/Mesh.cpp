#include "Mesh/Mesh.hpp"
#include "Elements/ElementFactory.hpp"
#include "Adjacency/Adjacency.hpp"
#include "Parallel//Communicator.hpp"
#include <functional>
#ifdef WITH_MPI
#include <metis.h>
#endif
#include <algorithm>  
#include "Utils/VectorUtils.hpp"

namespace PAMELA
{
	Mesh::~Mesh()
	{
		delete m_PolyhedronProperty_double;
		delete m_PolyhedronProperty_int;
		delete m_AdjacencySet;

	}

	Mesh::Mesh() : m_PointCollection(PointCollection(ELEMENTS::FAMILY::POINT)),
	               m_LineCollection(LineCollection(ELEMENTS::FAMILY::LINE)),
	               m_PolygonCollection(PolygonCollection(ELEMENTS::FAMILY::POLYGON)),
	               m_PolyhedronCollection(PolyhedronCollection(ELEMENTS::FAMILY::POLYHEDRON)),
	               m_ImplicitPointCollection(PointCollection(ELEMENTS::FAMILY::POINT)), m_ImplicitLineCollection(LineCollection(ELEMENTS::FAMILY::LINE)),
	               m_PolyhedronProperty_double(new Property<PolyhedronCollection, double>(&m_PolyhedronCollection)),
	               m_PolyhedronProperty_int(new Property<PolyhedronCollection, int>(&m_PolyhedronCollection)),
	               m_AdjacencySet(new AdjacencySet(this))
	{
	}

	void Mesh::CreateFacesFromCells()
	{

		LOGINFO("*** Creating Polygons from Polyhedra...");

		//Create adjacency while creating the faces
		ElementCollection<Polyhedron*>* source = &m_PolyhedronCollection;
		ElementCollection<Polygon*>* target = &m_PolygonCollection;
		ElementCollection<Polyhedron*>* base = &m_PolyhedronCollection;
		auto InitPolyhedronCollectionSize = target->size_all();
		Adjacency* adj = new Adjacency(ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POLYGON, ELEMENTS::FAMILY::POLYHEDRON,source, target, base);

		auto collectionSize = source->size_all();
		int nbFace = 0;
		int FaceIndex = 0;
		int PolyhedronIndex = 0;
		int nval = 0;
		int nrow = 0;

		adj->m_adjacencySparseMatrix->rowPtr.push_back(0);
		for (auto it = source->begin(); it!= source->end();++it)
		{
			Polyhedron* polyhedron = *it;
			PolyhedronIndex = polyhedron->get_localIndex();
			auto faces = polyhedron->CreateFaces();
			nbFace = static_cast<int>(faces.size());
			for (int j = 0; j < nbFace; j++)
			{
				auto returned_polygon = target->push_back_unique(faces[j]);
				FaceIndex = returned_polygon->get_localIndex();
				adj->m_adjacencySparseMatrix->columnIndex.push_back(FaceIndex);
				adj->m_adjacencySparseMatrix->values.push_back(PolyhedronIndex);
				nval++;
			}
			nrow++;
			adj->m_adjacencySparseMatrix->rowPtr[nrow] = nval;
		}
		adj->m_adjacencySparseMatrix->nnz = nval;
		adj->m_adjacencySparseMatrix->dimColumn = static_cast<int>(target->size_all());
		adj->m_adjacencySparseMatrix->sortRowIndexAndMoveValues();
		adj->m_adjacencySparseMatrix->checkMatrix();

		//Add to map
		m_AdjacencySet->TopologicalAdjacencyMap[std::make_tuple(source->get_family(), target->get_family(), base->get_family())] = adj;

		//
		LOGINFO(std::to_string(target->size_all() - InitPolyhedronCollectionSize) + " polygons have been created");
		LOGINFO("*** Done");
	}


	Point* Mesh::addPoint(ELEMENTS::TYPE elementType, int index, std::string groupLabel, double x, double y, double z)
	{
		Point* element = ElementFactory::makePoint(elementType, index, x, y, z);
		auto returnedElement = m_PointCollection.AddElement(groupLabel, element);
		if (element!=returnedElement)
		{
			//LOGWARNING("Try to add an existing element");
		}
		return returnedElement;
	}

	Point* Mesh::addPoint(std::string groupLabel, Point* point)
	{
		auto returnedElement = m_PointCollection.AddElement(groupLabel, point);
		if (point != returnedElement)
		{
			//LOGWARNING("Try to add an existing element");
		}
		return returnedElement;
	}

	Line* Mesh::addLine(ELEMENTS::TYPE elementType, int elementIndex, std::string groupLabel, const std::vector<Point*>& vertexList)
	{
		Line* element = ElementFactory::makeLine(elementType, elementIndex, vertexList);
		auto returnedElement = m_LineCollection.AddElement(groupLabel, element);
		if (element != returnedElement)
		{
			LOGWARNING("Try to add an existing element");
		}
		return returnedElement;
	}

	Polygon* Mesh::addPolygon(ELEMENTS::TYPE elementType, int elementIndex, std::string groupLabel, const std::vector<Point*>& vertexList)
	{
		Polygon* element = ElementFactory::makePolygon(elementType, elementIndex, vertexList);
		auto returnedElement = m_PolygonCollection.AddElement(groupLabel, element);
		if (element != returnedElement)
		{
			LOGWARNING("Try to add an existing element");
		}
		return returnedElement;
	}

	Polyhedron* Mesh::addPolyhedron(ELEMENTS::TYPE elementType, int elementIndex, std::string groupLabel, const std::vector<Point*>& vertexList)
	{
		Polyhedron* element = ElementFactory::makePolyhedron(elementType, elementIndex, vertexList);
		auto returnedElement = m_PolyhedronCollection.AddElement(groupLabel, element);
		if (element != returnedElement)
		{
			//LOGWARNING("Try to add an existing element");
			return returnedElement;
		}
		return nullptr;
		

	}

	void Mesh::AddImplicitLine(ELEMENTS::TYPE elementType, std::string groupLabel, std::vector<Point*>& pointList)
	{
		m_ImplicitPointCollection.AddElement(groupLabel, pointList[0]);
		for (size_t i=1;i!= pointList.size();++i)
		{
			m_ImplicitPointCollection.AddElement(groupLabel, pointList[i]);
			Line* element = ElementFactory::makeLine(ELEMENTS::TYPE::VTK_LINE, -1, { pointList[i-1],pointList[i] });
			m_ImplicitLineCollection.AddElement(groupLabel, element);
		}
	}


	void Mesh::PerformPolyhedronPartitioning(ELEMENTS::FAMILY edgeElement, ELEMENTS::FAMILY ghostBaseElement)
	{
		//MPI data
		auto CommRankSize = Communicator::worldSize();
		bool MPIRUN = Communicator::isMPIrun();
		int ipartition = Communicator::worldRank();
		int npartition = Communicator::worldSize();

		//if ((CommRankSize > 1) && (MPIRUN))	//TO be removed for debugging
		//{

			LOGINFO("*** Perform partitioning...");

			//This is a cell partitioning
			ELEMENTS::FAMILY nodeElement = ELEMENTS::FAMILY::POLYHEDRON;

			//Elements affiliation - GLOBAL
			std::vector<int> PolyhedronAffiliation(m_PolyhedronCollection.size_all());

			//PARTITION WISE
			std::set<int> PolyhedronOwned;
			std::set<int> PolygonOwned;
			std::set<int> LineOwned;
			std::set<int> PointOwned;

			std::set<int> PolyhedronGhost;
			std::set<int> PolygonGhost;
			std::set<int> LineGhost;
			std::set<int> PointGhost;

			//Get adjacencies
			auto adjacencyForPartitioning = getAdjacencySet()->get_TopologicalAdjacency(nodeElement, nodeElement, edgeElement);
			auto adjacencyForGhosts = getAdjacencySet()->get_TopologicalAdjacency(nodeElement, nodeElement, ghostBaseElement);

			//Partitioning
			if ((CommRankSize > 1) && (MPIRUN))
			{
				//Compute Partionioning vector from METIS
				LOGINFO("METIS partioning...");
				PolyhedronAffiliation = METISPartitioning(adjacencyForPartitioning, CommRankSize);
			}
			else
			{
				//Compute TRIVIAL Partionioning for one partition
				LOGINFO("TRIVIAL partioning...");
				PolyhedronAffiliation = TRIVIALPartitioning();
			}

			//PolyhedronAffiliation = METISPartitioning(adjacencyForPartitioning, 2);

			int nbPolyhedronInPartition = static_cast<int>(std::count(PolyhedronAffiliation.begin(), PolyhedronAffiliation.end(), ipartition));

			//POLYHEDRON
			//--OWNED POLYHEDRA
			int ind = 0;
			for (size_t i = 0; i != PolyhedronAffiliation.size(); ++i)
			{
				if (PolyhedronAffiliation[i] == ipartition)
				{
					PolyhedronOwned.insert(static_cast<int>(i));
					ind++;
				}
			}

			//--GHOST POLYHEDRA
			for (auto it = PolyhedronOwned.begin(); it != PolyhedronOwned.end(); ++it)
			{
				auto ele_adj = adjacencyForGhosts->get_SingleElementAdjacency(*it);
				for (auto it2 = ele_adj.first.begin(); it2 < ele_adj.first.end(); ++it2)
				{
					if (PolyhedronAffiliation[*it2] != ipartition)
					{
						PolyhedronGhost.insert(*it2);
					}
				}
			}

			LOGINFO("Ghost elements...");

			////OWNED AND GHOST POLYGONS   //TODO Can be much more efficient as it goes multiple times to the same point right now
			auto PolygonPolyhedronAdj = getAdjacencySet()->get_TopologicalAdjacency(ELEMENTS::FAMILY::POLYGON, ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POLYHEDRON);
			auto PolyhedronPolygonAdj = getAdjacencySet()->get_TopologicalAdjacency(ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POLYGON, ELEMENTS::FAMILY::POLYHEDRON);
			for (auto it = PolyhedronOwned.begin(); it != PolyhedronOwned.end(); ++it)
			{
				auto adj_Polyhedron2Polygon = PolyhedronPolygonAdj->get_SingleElementAdjacency(*it);
				auto adj_Polyhedron2PolygonSize = adj_Polyhedron2Polygon.first.size();
				for (size_t i = 0; i < adj_Polyhedron2PolygonSize; ++i)
				{
					auto adj_Polygon2Polyhedron = PolygonPolyhedronAdj->get_SingleElementAdjacency(adj_Polyhedron2Polygon.first[i]);
					auto adj_Polygon2PolyhedronSize = adj_Polygon2Polyhedron.first.size();
					auto PolyToPart = vectorUtils::Vector2VectorMapping(adj_Polygon2Polyhedron.first, PolyhedronAffiliation);
					if ((std::equal(PolyToPart.begin() + 1, PolyToPart.end(), PolyToPart.begin())) || PolyToPart.size() == 1)
					{
						//All points connect to polyhedra of the current partition
						PolygonOwned.insert(adj_Polyhedron2Polygon.first[i]);
					}
					else
					{

						int ival = CoinToss(PolyToPart[0], PolyToPart[1]);

						if (ival == ipartition)
						{
							PolygonOwned.insert(adj_Polyhedron2Polygon.first[i]);
						}
						else
						{
							PolygonGhost.insert(adj_Polyhedron2Polygon.first[i]);
						}

					}
				}
			}

			////OWNED AND GHOST POINTS   //TODO Can be much more efficient as it goes multiple times to the same point right now
			auto PointPolyhedronAdj = getAdjacencySet()->get_TopologicalAdjacency(ELEMENTS::FAMILY::POINT, ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POLYHEDRON);
			auto PolyhedronPointAdj = getAdjacencySet()->get_TopologicalAdjacency(ELEMENTS::FAMILY::POLYHEDRON, ELEMENTS::FAMILY::POINT, ELEMENTS::FAMILY::POLYHEDRON);
			for (auto it = PolyhedronOwned.begin(); it != PolyhedronOwned.end(); ++it)
			{
				auto adj_Poly2Point = PolyhedronPointAdj->get_SingleElementAdjacency(*it);
				int adj_Poly2PointSize = static_cast<int>(adj_Poly2Point.first.size());
				for (auto i = 0; i < adj_Poly2PointSize; ++i)
				{
					auto adj_Point2Poly = PointPolyhedronAdj->get_SingleElementAdjacency(adj_Poly2Point.first[i]);
					int adj_Point2PolySize = static_cast<int>(adj_Point2Poly.first.size());
					auto PolyToPart = vectorUtils::Vector2VectorMapping(adj_Point2Poly.first, PolyhedronAffiliation);
					if ((std::equal(PolyToPart.begin() + 1, PolyToPart.end(), PolyToPart.begin())) || PolyToPart.size() == 1)
					{
						//All points connect to polyhedra of the current partition
						PointOwned.insert(adj_Poly2Point.first[i]);
					}
					else
					{
						int ival = vectorUtils::MostOccuringValue(PolyToPart);


						if (ival == ipartition)
						{
							//The majority of points connect to polyhedra that belongs to the current partition
							PointOwned.insert(adj_Poly2Point.first[i]);
						}
						else
						{
							PointGhost.insert(adj_Poly2Point.first[i]);
						}

					}
				}
			}


			//ClearAfterPartitioning
			LOGINFO("Clean mesh...");
			m_PolyhedronCollection.ClearAfterPartitioning(PolyhedronOwned, PolyhedronGhost);
			m_PolygonCollection.ClearAfterPartitioning(PolygonOwned, PolygonGhost);
			m_PointCollection.ClearAfterPartitioning(PointOwned, PointGhost);
			m_PolyhedronProperty_double->ClearAfterPartitioning(PolyhedronOwned, PolyhedronGhost);
			m_PolyhedronProperty_int->ClearAfterPartitioning(PolyhedronOwned, PolyhedronGhost);
			LOGINFO("*** Done...");
			LOGINFO("Clean Adjacency...");
			m_AdjacencySet->ClearAfterPartitioning(PolyhedronOwned, PolyhedronGhost,PolygonOwned, PolygonGhost);
			LOGINFO("*** Done...");

		//}
	}


	std::vector<int> Mesh::METISPartitioning(Adjacency* adjacency, int npartition)
	{

#ifdef WITH_METIS

		ASSERT(adjacency->get_sourceElementCollection() == adjacency->get_targetElementCollection(), "Partitioning can only be done with adjacency of same elements");
		ASSERT(adjacency->get_sourceElementCollection()->size_all() > npartition, "Number of mesh elements must be greater than the number of partitions");

		auto csrMatrix = adjacency->get_adjacencySparseMatrix();


		// make sure locally we use METIS's types (which are in global namespace) and not grid::<type>
		using idx_t = ::idx_t;
		using real_t = ::real_t;

		int options[METIS_NOPTIONS];
		METIS_SetDefaultOptions(options);

		// Some type casts and constants
		idx_t nnodes = static_cast<idx_t>(adjacency->get_sourceElementCollection()->size_all());
		idx_t nparts = npartition;
		idx_t nconst = 1;
		idx_t objval = 0;
		std::vector<int> partitionVector(nnodes);

		METIS_PartGraphRecursive(&nnodes, &nconst, &csrMatrix->rowPtr[0], &csrMatrix->columnIndex[0],
			nullptr, nullptr, nullptr, &npartition, nullptr, nullptr, options, &objval, partitionVector.data());

		return partitionVector;

#else
		LOGERROR("METIS partitioner is not available");
		return {};
#endif

	}

	std::vector<int> Mesh::TRIVIALPartitioning()
	{
		std::vector<int> val(m_PolyhedronCollection.size_all(), 0);
		return val;
	}

	void Mesh::CreateLineGroupWithAdjacency(std::string Label, Adjacency* adjacency)
	{

		auto& line_collection = m_LineCollection;
		auto& point_collection = m_PointCollection;

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
			int isource = 0, itarget = 0, ipoint = static_cast<int>(m_PointCollection.size_owned()) , iline = static_cast<int>(m_LineCollection.size_owned());
			int cpt = 0;
			for (auto irow = 0; irow != dimRow; ++irow)
			{
				auto it = get_PolyhedronCollection()->begin_owned() + irow;
				auto xyz1 = (*it)->get_centroidCoordinates();
				auto source_point = ElementFactory::makePoint(ELEMENTS::TYPE::VTK_VERTEX, ipoint, xyz1[0], xyz1[1], xyz1[2]);
				auto source_rpoint = point_collection.AddElement(Label, source_point);
				++ipoint;
				itarget = isource;
				for (auto icol = rowPtr[irow]; icol != rowPtr[irow + 1]; ++icol)
				{
					if (icol!= columIndex[icol])
					{
						itarget = itarget + 1;
						auto it = get_PolyhedronCollection()->begin_owned() + columIndex[icol];
						auto xyz2 = (*it)->get_centroidCoordinates();
						auto target_point = ElementFactory::makePoint(ELEMENTS::TYPE::VTK_VERTEX, ipoint, xyz2[0], xyz2[1], xyz2[2]);
						auto target_rpoint = point_collection.AddElement(Label, target_point);
						++ipoint;
						auto edgev = { source_rpoint , target_rpoint };
						auto edge = ElementFactory::makeLine(ELEMENTS::TYPE::VTK_LINE, iline, edgev);
						++iline;
						auto redge = line_collection.AddElement(Label, edge);
					}
					
				}
				isource = itarget + 1;
				cpt++;
			}

		}

		m_LineCollection.activeGroup(Label);
		m_PointCollection.activeGroup(Label);


	}

}
