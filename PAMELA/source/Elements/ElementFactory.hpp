#pragma once

#include "Elements/Polyhedron.hpp"
#include "Elements/Element.hpp"
#include "Elements/Polygon.hpp"
#include "Elements/Line.hpp"
#include "Elements/Point.hpp"

namespace PAMELA
{

	class ElementFactory
	{

	public:

		//Makers
		static Point* makePoint(ELEMENTS::TYPE  elementType, int index, double x, double y, double z);
		static Line*  makeLine(ELEMENTS::TYPE  elementType, int index, const std::vector<Point*>& vertexList);
		static Polygon*  makePolygon(ELEMENTS::TYPE  elementType, int index, const std::vector<Point*>& vertexList);
		static Polyhedron* makePolyhedron(ELEMENTS::TYPE elementType, int index, const std::vector<Point*>& vertexList);

	protected:

		ElementFactory() = delete;


	};

}
