#include "MeshDataWriters/EnsightGoldWriter.hpp"
#include "Utils/Logger.hpp"
#include <iomanip>
namespace PAMELA
{

	namespace EnsightGold
	{

		/**
		 * \brief Initialize by classifying and mapping elements
		 */
		void EnsightGoldWriter::Init()
		{
			LOGINFO("*** Init Ensight Gold writer");
			//Number of digits for file extension
			m_nDigitsExtensionPartition = 5;
			m_nDigitsExtensionTime = 5;

			//Time
			m_currentTime = 0.0;
			m_currentTimeStep = 0;

			//Part Index
			int partIndex = 1;

			//------------------------------------------------------------- LineCollection -------------------------------------------------------------
			auto LineCollection = m_mesh->get_LineCollection();
			auto ActiveGroupMapLine = LineCollection->get_ActiveGroupsMap();

			//--Add active parts
			for (auto it = ActiveGroupMapLine.begin(); it != ActiveGroupMapLine.end(); ++it)	//Loop over group and act on active groups
			{
				if (it->second == true)
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
				if (it->second == true)
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
				if (it->second == true)
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


		/**
		 * \brief
		 */
		void EnsightGoldWriter::MakeCaseFile()
		{

			LOGINFO("*** Make Ensight Gold .case file");

			//Create file
			m_caseFile.open("./" + m_name + "_" + PartitionNumberForExtension() + ".case", std::fstream::in | std::fstream::out | std::fstream::trunc);

			int nb_files = Communicator::worldSize();

			//Header
			m_caseFile << "# Generated by PAMELA" << std::endl;
			m_caseFile << "FORMAT" << std::endl;
			m_caseFile << "type: ensight gold" << std::endl;
			m_caseFile << std::endl;
			m_caseFile << "GEOMETRY" << std::endl;
			m_caseFile << "model: " << m_name + "_" + PartitionNumberForExtension() + ".geo" << std::endl;
			m_caseFile << std::endl;

			std::string stars(m_nDigitsExtensionTime, '*');


			m_caseFile << "VARIABLE" << std::endl;

			//Variables
			if (m_Variable.size() != 0)
			{

				//Point variables
				for (auto it = m_PointParts.begin(); it != m_PointParts.end(); ++it)
				{
					auto part = it->second;

					//Per Element Variable
					for (auto it2 = part->PerElementVariable.begin(); it2 != part->PerElementVariable.end(); ++it2)
					{
						m_caseFile << "scalar per element:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << (*it2)->Label << "_" + stars << std::endl;
					}

					//Per Node Variable
					for (auto it2 = part->PerNodeVariable.begin(); it2 != part->PerNodeVariable.end(); ++it2)
					{
						m_caseFile << "scalar per node:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << (*it2)->Label << "_" + stars << std::endl;
					}
				}

				//Line variables
				for (auto it = m_LineParts.begin(); it != m_LineParts.end(); ++it)
				{
					auto part = it->second;

					//Per Element Variable
					for (auto it2 = part->PerElementVariable.begin(); it2 != part->PerElementVariable.end(); ++it2)
					{
						m_caseFile << "scalar per element:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}

					//Per Node Variable
					for (auto it2 = part->PerNodeVariable.begin(); it2 != part->PerNodeVariable.end(); ++it2)
					{
						m_caseFile << "scalar per node:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}
				}


				//Polygon variables
				for (auto it = m_PolygonParts.begin(); it != m_PolygonParts.end(); ++it)
				{
					auto part = it->second;

					//Per Element Variable
					for (auto it2 = part->PerElementVariable.begin(); it2 != part->PerElementVariable.end(); ++it2)
					{
						m_caseFile << "scalar per element:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}

					//Per Node Variable
					for (auto it2 = part->PerNodeVariable.begin(); it2 != part->PerNodeVariable.end(); ++it2)
					{
						m_caseFile << "scalar per node:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}
				}


				//Polyhedron variables
				for (auto it = m_PolyhedronParts.begin(); it != m_PolyhedronParts.end(); ++it)
				{
					auto part = it->second;

					//Per Element Variable
					for (auto it2 = part->PerElementVariable.begin(); it2 != part->PerElementVariable.end(); ++it2)
					{
						m_caseFile << "scalar per element:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}

					//Per Node Variable
					for (auto it2 = part->PerNodeVariable.begin(); it2 != part->PerNodeVariable.end(); ++it2)
					{
						m_caseFile << "scalar per node:" << "     " << 1 << std::setw(5);
						m_caseFile << "     " << (*it2)->Label << " " << part->Label << "_" << (*it2)->Label << "_" + stars << std::endl;
					}
				}
			}
			else
			{
				LOGWARNING("No variables found. Add variable before making case file");
			}

			//Time
			m_caseFile << std::endl;
			m_caseFile << "TIME" << std::endl;
			m_caseFile << "time set: 1" << std::endl;
			m_caseFile << "number of steps: 1" << std::endl;
			m_caseFile << "filename start number: 0" << std::endl;
			m_caseFile << "filename increment: 1" << std::endl;
			m_caseFile << "time values:" << std::endl;
			m_caseFile << 0 << std::endl;

			LOGINFO("*** Done");
		}

		/**
		 * \brief
		 */
		void EnsightGoldWriter::MakeGeoFile()
		{
			LOGINFO("*** Init Ensight Gold .geo file");

			//Create file
			m_geoFile.open("./" + m_name + "_" + PartitionNumberForExtension() + ".geo", std::fstream::in | std::fstream::out | std::fstream::trunc);

			//Header
			MakeGeoFile_Header();

			//-- Line
			MakeGeoFile_AddParts(&m_LineParts);

			//-- Polygon
			MakeGeoFile_AddParts(&m_PolygonParts);

			//-- Polyhedron
			MakeGeoFile_AddParts(&m_PolyhedronParts);

			LOGINFO("*** Done");
		}


		void EnsightGoldWriter::SetVariable(std::string label, double univalue)
		{

			for (auto const& part : m_PolyhedronParts)
			{
				auto var = m_Variable.at(VariableKey(label,part.first));
				var->set_data(univalue);
			}
		}


		/**
		 * \brief
		 * \param label
		 * \param univalue
		 */
		void EnsightGoldWriter::SetVariable(std::string label, std::string part, double univalue)
		{

			auto var = m_Variable.at(VariableKey(label, part));
			var->set_data(univalue);
		}



		void EnsightGoldWriter::SetVariable(std::string label, const std::vector<double>& values)
		{
			auto var = m_Variable.at(VariableKey(label, m_PolyhedronParts.begin()->first));
			var->set_data(values);
		}


		void EnsightGoldWriter::SetVariable(std::string label, std::string part, const std::vector<double>& values)
		{
			auto var = m_Variable.at(VariableKey(label, part));
			var->set_data(values);
		}

		/**
		 * \brief
		 */
		void EnsightGoldWriter::DumpVariables()
		{

			//Polygon
			DumpVariables_Parts(&m_PolygonParts);

			//Polyhedron
			DumpVariables_Parts(&m_PolyhedronParts);

		}


		void EnsightGoldWriter::MakeGeoFile_Header()
		{

			//Header
			m_geoFile << "EnSight Model Geometry File" << std::endl;
			m_geoFile << "EnSight 7.1.0" << std::endl;

			//Node id
			m_geoFile << "node id given" << std::endl;

			//Element id
			m_geoFile << "element id given" << std::endl;

		}

		void EnsightGoldWriter::CreateVariable(FAMILY family, VARIABLE_TYPE dtype, VARIABLE_LOCATION dloc, std::string name, std::string part)
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

		void EnsightGoldWriter::CreateVariable(FAMILY family, VARIABLE_TYPE dtype, VARIABLE_LOCATION dloc, std::string name)
		{

			std::string part;
			switch (family)
			{
			case FAMILY::POINT:
				for (auto const& part : m_PointParts)
				{
					CreateVariable(family, dtype, dloc, name, part.first);
				}
				break;
			case FAMILY::LINE:
				for (auto const& part : m_LineParts)
				{
					CreateVariable(family, dtype, dloc, name, part.first);
				}
				break;
			case FAMILY::POLYGON:
				for (auto const& part : m_PolygonParts)
				{
					CreateVariable(family, dtype, dloc, name, part.first);
				}
				break;
			case FAMILY::POLYHEDRON:
				for (auto const& part : m_PolyhedronParts)
				{
					CreateVariable(family, dtype, dloc, name, part.first);
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
		std::string EnsightGoldWriter::TimeStepNumberForExtension()
		{
			std::string Ext = std::to_string(m_currentTimeStep);
			int nExt = static_cast<int>(Ext.size());
			for (auto i = 0; i < (m_nDigitsExtensionTime - nExt); ++i)
			{
				Ext.insert(0, "0");
			}

			return  Ext;
		}

		/**
		* \brief
		* \return
		*/
		std::string EnsightGoldWriter::PartitionNumberForExtension()
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

}