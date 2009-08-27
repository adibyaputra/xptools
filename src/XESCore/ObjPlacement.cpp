/*
 * Copyright (c) 2004, Laminar Research.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */
#include "ObjPlacement.h"
#include "MeshDefs.h"
#include "MapDefs.h"
#include "AssertUtils.h"
#include "ParamDefs.h"
#include <CGAL/centroid.h>
#include "MapAlgs.h"
#include "CompGeomDefs2.h"
#include "CompGeomUtils.h"
#include "BWImage.h"
#include "NetTables.h"
#include "ObjTables.h"
#include "GISUtils.h"
#include "DEMTables.h"
#define XUTILS_EXCLUDE_MAC_CRAP 1
#include "XUtils.h"
#include "XESConstants.h"
#include "MapAlgs.h"
#include "MapPolygon.h"
#include "MapBuffer.h"


#if BENTODO
	um, we should have a better tool than OPENGL_MAP
#endif

/*

	todo:
		Redo the frequency/do-we-place logic
		Redo the subdivision logic
		Try to deal with the problem of subdivision failing due to lack of efficiency!
		-clearly objs are overlapping roads - why?  will using new inset help?
		-clearly too many objs for the space..not so good


		favor BIG objects??
		prioritize AGL features
		handle multiple point AGL features better!!
		create poly zoning on empty areas
		get floors right!

*/

#define MAJOR_LEN_MIN	50
#define MAJOR_LEN_MAX	1000
#define MINOR_LEN_MIN	10
#define MINOR_LEN_MAX	200
#define MAX_MAJOR_DIFF	150
#define MAX_MINOR_DIFF	50
#define MIN_LOT_SIZE	100
#define MAX_VERTICAL_PER_OBJ 5
#define MAX_MOVE_FEATURE 100.0			// Max distance to move a point feature toward the center of a polygon in meters...to try to fit it!
#define MAX_AREA_FOR_RANDOM 4000000.0f

#define MAX_VERTICAL_ERROR_FLOAT	2			// Max err or an object hanging off a slope
#define MAX_VERTICAL_ERROR_BURY		4			// Max err or an object hanging off a slope

#define MIN_OBJ_EFFICIENCY	0.5			// This is the minimum efficiency - we won't place an obj in a lot if it takes less than 50% of the space - it's too small of an obj.

// This causes subdivision to place the max number of facades - good for testing.
#define	FORCE_MAX_DENSITY	1

#define LOG_REMOVE_FEATURES 0

	static	int	feat_raster_try = 0;
	static	int	feat_raster_ok  = 0;
	static	int	fill_raster_try = 0;
	static	int	fill_raster_ok  = 0;
	static	int	total_subdiv = 0;




//static hash_map<int, int>	sPlacementCounts;


// Default scale to render is 1 pixel per meter
#define IDEAL_SCALE 1
// Allocate a 2kx2k scratch array
#define MAX_WIDTH 8192
#define MAX_HEIGHT 8192
// Wipe out objects within 10 meters of each other
#define	OVERLAP_MIN	10

static BWImage	gImage(MAX_WIDTH, MAX_HEIGHT);




/******************************************************************************************************************************
 ******************************************************************************************************************************
												C++ UTILITY HELPERS
 ******************************************************************************************************************************
 ******************************************************************************************************************************/

struct	BinaryIterator {

	int		max;
	int		granularity;
	int		now;

	BinaryIterator(int inmax)
	{
		max = inmax;
		granularity = 0;
		while ((1 << granularity) < max)
			++granularity;
		now = 0;
	}

	int operator*() const { return now; }
	bool operator()() const { return granularity >= 0; }
	BinaryIterator& operator++() { inc(); return *this; }
	BinaryIterator& operator++(int) { inc(); return *this; }

	void inc(void)
	{
		while (1)
		{
			now += (1 << granularity);
			if (now >= max)
			{
				--granularity;
				now = 0;
				if (granularity < 0) return;
			}

			int mask = (1 << (granularity+1)) - 1;
			if (now & mask) break;
		}
	}
};



template <class T>
int IndexOfBiggest(T * begin, T * end)
{
	if (begin == end) return 0;
	int n = 0;
	T best = *begin;
	++begin;
	while (begin != end)
	{
		if (*begin > best)
		{
			best = *begin;
		}
		++n;
		++begin;
	}
	return n;
}

template <class V>
void RandomizeVector(V& vec)
{
	for (int n = 0; n < vec.size(); ++n)
	{
		swap(vec[rand() % vec.size()],vec[rand() % vec.size()]);
	}
}



/******************************************************************************************************************************
 ******************************************************************************************************************************
												MISC ACCESSORS AND HELPERS
 ******************************************************************************************************************************
 ******************************************************************************************************************************/

#pragma mark -

static void MyAntennaFunc(int n, void * ref)
{
	vector<double> * a = (vector<double>*) ref;
	a->insert(a->begin()+n, (*a)[n]);
}

// Rotate and offset a polygon for the purpose of placement.
static	void	RotateAndOffset(Polygon2& ioPolygon, const Vector2& offset, float heading)
{
	float 	cosv = cos(heading * DEG_TO_RAD);
	float 	sinv = sin(heading * DEG_TO_RAD);
	for (Polygon2::iterator i = ioPolygon.begin(); i != ioPolygon.end(); ++i)
	{
		Point2	p;

		p.y_ = i->y() * cosv - i->x() * sinv;
		p.x_ = i->x() * cosv + i->y() * sinv;
		p += offset;
		(*i) = p;
	}
}

// Given a poly obj, try to detect that it's gone wrong.
static bool	SanityCheck(const GISPolyObjPlacement_t& inPlacement)
{
#if 0
	vector<Polygon_with_holes_2>	poly(inPlacement.mShape);
	CoordTranslator		trans;
	CreateTranslatorForPolygon(poly[0], trans);
	for (int n = 0; n < poly[0].size(); ++n)
	{
		poly[0][n] = trans.Forward(poly[0][n]);
	}

	double area = poly[0].area();
	if (area < 0) {
		printf("Rejecting polygon - area less than zero.\n");
		return false;
	}
	if (area > 1000000.0) {
		printf("Rejecting polygon - area greater than mil sqm.\n");
		return false;
	}

	for (int n = 0; n < poly[0].size(); ++n)
	if (poly[0].side(n).squared_length() > 1000000.0)
	{
		printf("Rejecting polygon - side longer than 1 km.\n");
		return false;
	}
#endif
	return true;
}

// Returns the height or -1 for any automatically.
static int GetPossibleFeatureHeight(const GISPointFeature_t& f)
{
	GISParamMap::const_iterator i = f.mParams.find(pf_Height);
	if (i == f.mParams.end()) return -1;
	return i->second;
}

// Given two features, which is higher priority - prefer airport furniture most, then
// things with AGL.
static bool	LowerPriorityFeature(GISPointFeature_t& lhs, GISPointFeature_t& rhs)
{
	if (Feature_IsAirportFurniture(lhs.mFeatType) && Feature_IsAirportFurniture(rhs.mFeatType))
		return lhs.mFeatType < rhs.mFeatType;

	if (Feature_IsAirportFurniture(lhs.mFeatType)) return false;
	if (Feature_IsAirportFurniture(rhs.mFeatType)) return true;

	bool	left_has_height = lhs.mParams.find(pf_Height) != lhs.mParams.end();
	bool	right_has_height = rhs.mParams.find(pf_Height) != rhs.mParams.end();
	if (left_has_height && right_has_height) return lhs.mParams[pf_Height] < rhs.mParams[pf_Height];
	if (left_has_height) return false;
	if (right_has_height) return true;

	return lhs.mFeatType < rhs.mFeatType;
}

// Given a raod segment, return the widest type - this is the one we must use for insetting.
static int	WidestRoadTypeForSegment(Pmwx::Halfedge_const_handle he)
{
	int	best_type = NO_VALUE;
	double best_width = 0.0;
	for (GISNetworkSegmentVector::const_iterator i = he->data().mSegments.begin(); i != he->data().mSegments.end(); ++i)
	{
		if (gNetEntities[i->mRepType].width > best_width)
		{
			best_type = i->mRepType;
			best_width = gNetEntities[i->mRepType].width;
		}
	}

	for (GISNetworkSegmentVector::const_iterator i = he->twin()->data().mSegments.begin(); i != he->twin()->data().mSegments.end(); ++i)
	{
		if (gNetEntities[i->mRepType].width > best_width)
		{
			best_type = i->mRepType;
			best_width = gNetEntities[i->mRepType].width;
		}
	}
	return best_type;
}

// This primitive tells us if a given two edges are effectively one edge - basically
// we're trying to ignore near-colllinear edges for subdivision
static bool	CanSkipSegment(Halfedge_handle prev, Halfedge_handle edge)
{
	if (WidestRoadTypeForSegment(edge) != WidestRoadTypeForSegment(prev))
		return false;
	Vector_2	v1(edge->source()->point(), edge->target()->point());
	Vector_2	v2(prev->source()->point(), prev->target()->point());
	return (normalize(v1) * normalize(v2) > 0.99);
}

// Given an objct type, build a polygon for it.
static bool FetchObjectBoundaryByRep(int tp, Polygon2 * outPoly, float scale, float * outWidth, float * outDepth)
{
	RepFeatureIndex::iterator i = gRepFeatureIndex.find(tp);
	if (i == gRepFeatureIndex.end()) return false;
	int index = i->second;
	float width = gRepTable[index].width_min;
	float depth = gRepTable[index].depth_min;
	width *= scale;
	depth *= scale;
	if (outWidth) *outWidth = width;
	if (outDepth) *outDepth = depth;
	width *= 0.5;
	depth *= 0.5;
	outPoly->resize(4);
	(*outPoly)[0].x_ = -width;	(*outPoly)[0].y_ = -depth;
	(*outPoly)[1].x_ = -width;	(*outPoly)[1].y_ =  depth;
	(*outPoly)[2].x_ =  width;	(*outPoly)[2].y_ =  depth;
	(*outPoly)[3].x_ =  width;	(*outPoly)[3].y_ = -depth;
	return true;
}

// Given an arbitrary quad, determine the largest rectangular lot fitted to the front face.
static void	GetLotDimensions(const Point_2& p0, const Point_2& p1, const Point_2& p2, const Point_2& p3, float * outWidth, float * outDepth)
{
	// Form vectors along the front (in both dirs) and sides.
	Vector_2	v01(p0, p1);
	Vector_2	v10(p1, p0);
	Vector_2	v12(p1, p2);
	Vector_2	v03(p0, p3);

	// If the quad opens away from us, the front side limits our side.
	Point_2	lim0 = p0;
	Point_2	lim1 = p1;
	// But if the sides move in, we project our back onto the front to figure out how 'squeezed' we are due to the back.,
	// NOTE this is not perfect - the farthest back corner doesn't totally pinch us.
	if (CGAL::to_double(v01 * v03) > 0.0) lim0 = Line_2(p0, v01).projection(p3);
	if (CGAL::to_double(v10 * v12) > 0.0) lim0 = Line_2(p1, v10).projection(p2);

	//if (CGAL::to_double(v01 * v03) > 0.0)		lim0 = p0 + v01.projection(v03);
	//if (CGAL::to_double(v10 * v12) > 0.0)		lim1 = p1 + v10.projection(v12);

	*outWidth = sqrt(CGAL::to_double(Segment_2(lim0, lim1).squared_length()));

	// Also project sides onto a vector towards the back...the closer one limits us.
	Vector_2	perp = v01.perpendicular(CGAL::CLOCKWISE);

	// Is this right?
	// lim2 is the projection of p2 on the line perpendicular to (p0, p1) through p1
	Point_2 lim2 = Line_2(p1, perp).projection(p2);
	Point_2 lim3 = Line_2(p0, perp).projection(p3);
	//Point_2	lim2 = p1 + perp.projection(v12);
	//Point_2	lim3 = p0 + perp.projection(v03);

	double d1 = CGAL::to_double(Segment_2(p0, lim3).squared_length());
	double d2 = CGAL::to_double(Segment_2(p1, lim2).squared_length());
	*outDepth = sqrt(min(d1, d2));
}

void FindSlopeLimits(float normal[3], double heading_dx, double heading_dy, double max_err_up, double max_err_down, double& max_width_slope, double& max_depth_slope)
{
	Vector3	nrml(normal[0], normal[1], normal[2]);
	Vector3	long_axis(heading_dx, heading_dy, 0.0);
	Vector3	lat_axis(heading_dy, -heading_dx, 0.0);

	Vector3	fall_long = nrml.cross(lat_axis);
	Vector3	fall_lat = long_axis.cross(nrml);

	fall_long.normalize();
	fall_lat.normalize();

	if (fall_lat.dz != 0.0)  max_width_slope = (fall_lat.dz < 0.0 ? -max_err_down : max_err_up) * sqrt(fall_lat.dx * fall_lat.dx + fall_lat.dy * fall_lat.dy) / fall_lat.dz; 		else max_width_slope = 9.9e9;
	if (fall_long.dz != 0.0) max_depth_slope = (fall_long.dz < 0.0 ? -max_err_down : max_err_up) * sqrt(fall_long.dx * fall_long.dx + fall_long.dy * fall_long.dy) / fall_long.dz; 	else max_depth_slope = 9.9e9;

}

bool VerticalOkay(vector<Polygon2>& bounds, const Point2& p, CoordTranslator2& trans, const DEMGeo& ele)
{
	float mstr = ele.value_linear(p.x(), p.y());
	Point2 p1 = trans.Reverse(bounds[0][0]);
	Point2 p2 = trans.Reverse(bounds[0][1]);
	Point2 p3 = trans.Reverse(bounds[0][2]);
	Point2 p4 = trans.Reverse(bounds[0][3]);
	float e1 = ele.value_linear(p1.x(),p1.y());
	float e2 = ele.value_linear(p2.x(),p2.y());
	float e3 = ele.value_linear(p3.x(),p3.y());
	float e4 = ele.value_linear(p4.x(),p4.y());
	float mi = min(min(e1,e2),min(e3,e4));
	float ma = max(max(e1,e2),max(e3,e4));
	mi = min(mi, mstr);
	ma = max(ma, mstr);

	return (ma - mstr) < MAX_VERTICAL_PER_OBJ &&
			(mstr - mi) < MAX_VERTICAL_PER_OBJ;
}

inline double remap_tval(double t)
{
	t += 0.5;
	if (t > 1.0) t -= 1.0;
	return t;
}

int highest_prio_tri(const CDT::Face_handle& tri)
{
	int lu = tri->info().terrain;
	for (set<int>::iterator border = tri->info().terrain_border.begin(); border != tri->info().terrain_border.end(); ++border)
	{
		if (LowerPriorityNaturalTerrain(lu, *border))
			lu = *border;
	}
	return lu;
}

/******************************************************************************************************************************
 * SUBDIVISION PLACEMENT
 ******************************************************************************************************************************/

#pragma mark -

/*
 * CanUseSubdivision
 *
 * Determine if a lot is suitable for subdivision.  This is based on t he number of sides and the relative
 * size of the lot.
 *
 */
bool	CanUseSubdivision(Face_handle inFace)
{
	if (inFace->is_unbounded()) return false;
	if (inFace->holes_begin() != inFace->holes_end()) return false;
	if (!inFace->data().mObjs.empty()) return false;
	if (!inFace->data().mPolyObjs.empty()) return false;


	/* Check for 4 sides. */
	Pmwx::Ccb_halfedge_circulator prev, stop, iter;
	stop = iter = inFace->outer_ccb();
	int count = 0;
	Polygon_2	outer;
	do {
		prev = iter;
		++iter;
		if (!CanSkipSegment(prev, iter))
		{
			if (count >= 4) return false;
			outer.push_back(iter->source()->point());
			++count;
		}
	} while (stop != iter);
	if (count != 4) return false;

	/* Calculate side dimensions. */
	double lens[4];
	double	LON_FACTOR = cos(CGAL::to_double(iter->source()->point().y()) * DEG_TO_RAD);
	double	DEG_TO_MTR_LON = DEG_TO_MTR_LAT * LON_FACTOR ;
	for (int n = 0; n < 4; ++n)
	{
		double	x1 = CGAL::to_double(outer[n].x());
		double	y1 = CGAL::to_double(outer[n].y());
		double	x2 = CGAL::to_double(outer[(n+1)%4].x());
		double	y2 = CGAL::to_double(outer[(n+1)%4].y());

		lens[n] = (LonLatDistMetersWithScale(x1, y1, x2, y2, DEG_TO_MTR_LON, DEG_TO_MTR_LAT));
	}

	int p0 = IndexOfBiggest(lens,lens+4);
	int p1 = (p0+1)%4;
	int p2 = (p0+2)%4;
	int p3 = (p0+3)%4;

	/* Check relative size lengths */
	if (fabs(lens[p0] - lens[p2]) > MAX_MAJOR_DIFF)	return false;
	if (fabs(lens[p1] - lens[p3]) > MAX_MINOR_DIFF)	return false;
	if (lens[p0] < MAJOR_LEN_MIN ||
		lens[p2] < MAJOR_LEN_MIN ||
		lens[p0] > MAJOR_LEN_MAX ||
		lens[p2] > MAJOR_LEN_MAX ||
		lens[p1] < MINOR_LEN_MIN ||
		lens[p3] < MINOR_LEN_MIN ||
		lens[p1] > MINOR_LEN_MAX ||
		lens[p3] > MINOR_LEN_MAX) return false;

	/* Check shape */
	if (!outer.is_convex()) return false;
//	if (outer.area() < MIN_LOT_SIZE) return false;	// AREA IS NOT IN LOCAL COORDINATES?!?!!

	/* Make sure we can encode every  feature as a facade (we need to fix this). */
//	for (GISPointFeatureVector::iterator feat = inFace->mPointFeatures.begin(); feat != inFace->mPointFeatures.end(); ++feat)
//	{
//		if (gRepTable[gRepFeatureIndex[feat->mFeatType]].fac_allow == 0) return false;
//	}
	return true;
}

/*
 * ProcessOneLot
 *
 * Given a lot attempet to stick something on it, return true if we populate it.
 *
 */
bool	ProcessOneLot(
					Face_handle		 		owner, 			// The owner of this lot
					CoordTranslator_2& 		coords,			// A coord translator frmo lat/lon to meters
					const Polygon_2&			lot,			// The lot, four-sided
					const float *			densities,		// density of each side
					bool					leaf,			// Is this beyond the subdivision point?
					const DEMGeoMap& 		dems,
					const CDT&				mesh)
{
	/****************************************************************
	 * PREP
	 ****************************************************************/

		float	max_road_density = densities[0];
		int		i;
		double	sidelens[4];
		double	min_side, max_side;
		int		query;
		int		choices;

	/* Fetch all parameters needed to do the population operation. */
	for (i = 0; i < 4; ++i)
		sidelens[i] = sqrt(CGAL::to_double(lot.edge(i).squared_length()));
	min_side = max_side = sidelens[0];

	for (i = 1; i < 4; ++i)
	{
		max_road_density = max(max_road_density, densities[i]);
		min_side = min(min_side, sidelens[i]);
		max_side = max(max_side, sidelens[i]);
	}

	Point_2	cent = coords.Reverse(CGAL::centroid(lot.vertices_begin(), lot.vertices_end()));

	int	terrain = NO_VALUE;

	static int hint_id = CDT::gen_cache_key();
	CDT::Locate_type lt;
	CDT::Face_handle recent = NULL;
	recent = mesh.locate_cache(cent, lt, i, hint_id);
	if (lt == CDT::FACE || lt == CDT::EDGE || lt == CDT::VERTEX)
	{
		terrain = highest_prio_tri(recent);
	} else {
		DebugAssert(!"Hrm....object not found on terrain mesh...");
	}

//	int	landuse = dems[dem_LandUse].xy_nearest(centroid.x, centroid.y);
//	int	climate = dems[dem_Climate].xy_nearest(centroid.x, centroid.y);
//	int	zoning = owner->mTerrainType;

//	float elev 			= dems[dem_Elevation	].value_linear(centroid.x, centroid.y);
//	float temp 			= dems[dem_Temperature	].value_linear(centroid.x, centroid.y);
//	float slope 		= dems[dem_Slope		].value_linear(centroid.x, centroid.y);
//	float relelev 		= dems[dem_RelativeElevation].value_linear(centroid.x, centroid.y);
//	float elevrange 	= dems[dem_ElevationRange].value_linear(centroid.x, centroid.y);
//	float urban_dense 	= dems[dem_UrbanDensity	].value_linear(centroid.x, centroid.y);
//	float urban_prop 	= dems[dem_UrbanPropertyValue].value_linear(centroid.x, centroid.y);
//	float urban_radial 	= dems[dem_UrbanRadial	].value_linear(centroid.x, centroid.y);
//	float urban_trans	= dems[dem_UrbanTransport].value_linear(centroid.x, centroid.y);

	int		require_feat = NO_VALUE;
	float	required_agl = -1.0;

//	max_road_density *= urban_dense;

	/* Look for any features that might be in the area.  We need this to populate features! */
	int	features_in_area = 0;
	GISPointFeature_t * the_feature = NULL;
	for (GISPointFeatureVector::iterator feat = owner->data().mPointFeatures.begin(); feat != owner->data().mPointFeatures.end(); ++feat)
	if (IsWellKnownFeature(feat->mFeatType))
	{
		Point_2	local = coords.Forward(feat->mLocation);
		if (lot.bounded_side(local) != CGAL::ON_UNBOUNDED_SIDE)
		{
			++features_in_area;
			the_feature = &*feat;
		}
	}

	/* If we have two features in one lot, we cannot populate - this should bounce out to the subdivide, we hope. */
	if (features_in_area > 1) return false;
	if (features_in_area == 1)
	{
		require_feat = the_feature->mFeatType;
		if (the_feature->mParams.count(pf_Height))
			required_agl = the_feature->mParams[pf_Height];
	}

	/* Calculate some kind of height limitations. */

	float height_max;
	if (required_agl != -1)
		height_max = required_agl;
	else {
		if (owner->data().mParams.count(af_Height))
			height_max = owner->data().mParams[af_Height];
		else
			height_max = 0.0;
	}

	/****************************************************************
	 * FACADE PLACEMENT
	 ****************************************************************/
	// First see if we want to build a building here!  If we have a
	// feature we always do of course.
	if ((FORCE_MAX_DENSITY && max_road_density > 0.0) || require_feat != NO_VALUE || RollDice(max_road_density))
	{
		choices = QueryUsableFacsBySize(				// This is facade placement for subdivision,
								require_feat,			// with or without a feature!
								terrain,
								max_side,
								min_side,
								height_max,
								&query, 1);

		if (choices)
		{
			Polygon_2	facade;
			InsetPolygon_2(lot, NULL, RandRange(2.0,4.0), true, facade, NULL, NULL);
			GISPolyObjPlacement_t	place;
			for (i = 0; i < facade.size(); ++i)
			{
				place.mShape.outer_boundary().push_back(coords.Reverse(facade[i]));
			}
			place.mRepType = gRepTable[query].obj_name;
			place.mLocation = CGAL::centroid(place.mShape.outer_boundary().vertices_begin(),place.mShape.outer_boundary().vertices_end());
			place.mHeight = (required_agl == -1) ? RandRange(gRepTable[query].height_min, gRepTable[query].height_max) : required_agl;
			place.mDerived = require_feat != NO_VALUE;
			if (SanityCheck(place))
				owner->data().mPolyObjs.push_back(place);

			IncrementRepUsage(gRepTable[query].obj_name);
			return true;
		}
	}

	/****************************************************************
	 * OBJ PLACEMENT
	 ****************************************************************/
	for (int s = 0; s < 4; ++s)
	if ((FORCE_MAX_DENSITY && densities[s] > 0.0) || require_feat != NO_VALUE || RollDice(densities[s]))
	{
		float	width, depth;
		GetLotDimensions(
				lot[s	   ],
				lot[(s+1)%4],
				lot[(s+2)%4],
				lot[(s+3)%4],
				&width, &depth);

		double area = CGAL::to_double(lot.area());

		choices = QueryUsableObjsBySize(			// This is object placement for subdivision
								require_feat,		// whether we have a feature or not.
								terrain,
								width,
								depth,
								height_max,
								1, 0,
								&query, 1);
		if (choices)
		{
			float obj_area = gRepTable[query].width_max * gRepTable[query].depth_max;
			if (obj_area >= (area * MIN_OBJ_EFFICIENCY))
			{
				GISObjPlacement_t	obj;
				obj.mRepType = gRepTable[query].obj_name;
				obj.mLocation = cent;
				Vector_2	facing(lot[s],lot[(s+1)%4]);
				facing = normalize(facing);
				facing = facing.perpendicular(CGAL::CLOCKWISE);
				obj.mHeading = atan2(CGAL::to_double(facing.x()), CGAL::to_double(facing.y())) * RAD_TO_DEG;
				obj.mDerived = require_feat != NO_VALUE;
				owner->data().mObjs.push_back(obj);
				IncrementRepUsage(gRepTable[query].obj_name);
				return true;
			}
		}
	}

	/****************************************************************
	 * MISC
	 ****************************************************************/

	// If we didn't build a building, maybe we want to just subdivide?
	if (!leaf && (FORCE_MAX_DENSITY || !RollDice(1.0 - max_road_density))) return false;

//	if (RollDice(urban_prop))
//	{
//		// Do vege
//	} else {
//		// ??
//	}
	return true;
}

/*
 * ProcessLotRecursive
 *
 * Handle a lot by trying to process it.  If we fail, cut it in half along the short axis and try again.
 *
 */
bool	ProcessLotRecursive(
					Face_handle		 		owner, 			// The owner of this lot
					CoordTranslator_2& 		coords,			// A coord translator frmo lat/lon to meters
					const Polygon_2&			lot,			// The lot, four-sided
					const float *			densities,
					const DEMGeoMap&		dems,
					const CDT&				mesh)
{
	double	lot_size = CGAL::to_double(lot.area());
	bool	is_leaf = lot_size < MIN_LOT_SIZE;
	if (ProcessOneLot(owner,coords,lot,densities,is_leaf, dems, mesh))
		return true;

	if (!is_leaf)
	{
		// Create a numbering system where the longest side starts at "p0".
		int p0, i;
		FastKernel::FT m(0.0), l(0.0);
		for (i = 0; i < 4; i++) {
			l = lot.edge(p0).squared_length();
			if (l>m) {
				m=l;
				p0=i;
			}
		}
		int p1 = (p0 + 1) % 4;
		int p2 = (p0 + 2) % 4;
		int p3 = (p0 + 3) % 4;

		// We are going to form two quads.  The first one will start just after the cut point on
		// the longest side.  the second will start just after the cut on the side opposite the
		// longest side.
		Polygon_2		sub1, sub2;
		float			dense1[4], dense2[4];
		Point_2 cut1 = CGAL::midpoint(lot.edge(p0).source(),lot.edge(p0).target());
		Point_2 cut2 = CGAL::midpoint(lot.edge(p2).source(),lot.edge(p2).target());
		sub1.push_back(cut1);			dense1[0] = densities[p0];
		sub1.push_back(lot[p1]);		dense1[1] = densities[p1];
		sub1.push_back(lot[p2]);		dense1[2] = densities[p2];
		sub1.push_back(cut2);			dense1[3] = 0.0;
		sub2.push_back(cut2);			dense2[0] = densities[p2];
		sub2.push_back(lot[p3]);		dense2[1] = densities[p3];
		sub2.push_back(lot[p0]);		dense2[2] = densities[p0];
		sub2.push_back(cut1);			dense2[3] = 0.0;

		bool ok1 = ProcessLotRecursive(owner, coords, sub1, dense1, dems, mesh);
		bool ok2 = ProcessLotRecursive(owner, coords, sub2, dense2, dems, mesh);
		return ok1 && ok2;
	}

	return false;
}

/*
 * SubdivideFace
 *
 * This routine places objects on a polygon by subdivision.
 * Preqrequisit - the face has four sides, no wholes, and is convex.
 *
 */
bool	SubdivideFace(
							Face_handle		 	inFace,
							const DEMGeoMap& 	inDEMs,
							const CDT&			inMesh)
{
		Polygon_2 						perimeter;
		Polygon_2 						perimeter_inset;
		CoordTranslator_2					translator;
		int								road_types[4];
		double							road_widths[4];
		float							road_densities[4];
		Pmwx::Ccb_halfedge_circulator	prev, iter, stop;

	// First we need to work out the extent of our face and set up coordinates
	translator.mSrcMin =  Point_2(180.0, 90.0);
	translator.mSrcMax = Point_2(-180.0, -90.0);

	iter = stop = inFace->outer_ccb();
	int n = 0;
	do {
		prev = iter;
		++iter;
		if (!CanSkipSegment(prev, iter))
		{
			DebugAssert(n < 4);
			perimeter.push_back(iter->source()->point());
			translator.mSrcMin = Point_2(min(CGAL::to_double(translator.mSrcMin.x()), CGAL::to_double(perimeter[n].x())), min(CGAL::to_double(translator.mSrcMin.y()), CGAL::to_double(perimeter[n].y())));
			translator.mSrcMax = Point_2(max(CGAL::to_double(translator.mSrcMax.x()), CGAL::to_double(perimeter[n].x())), max(CGAL::to_double(translator.mSrcMax.y()), CGAL::to_double(perimeter[n].y())));
			road_types[n] = WidestRoadTypeForSegment(iter);
			++n;
		}
	} while (iter != stop);

	translator.mDstMin = Point_2(0, 0);
	translator.mDstMax = Point_2(CGAL::to_double(translator.mSrcMax.x() - translator.mSrcMin.x()) * DEG_TO_MTR_LAT * cos(CGAL::to_double(translator.mSrcMin.y() + translator.mSrcMax.y()) * 0.5 * DEG_TO_RAD),
								 CGAL::to_double(translator.mSrcMax.y() - translator.mSrcMin.y()) * DEG_TO_MTR_LAT);

	// Translate the face and work out densities.
	for(Polygon_2::Vertex_iterator v = perimeter.vertices_begin(); v != perimeter.vertices_end(); ++v)
//	for (n = 0; n < 4; ++n)
//	{
	*v = translator.Forward(*v);
//		perimeter[n] = translator.Forward(perimeter[n]);
//	}
	for (n = 0; n < 4; ++n)
	{
		Point_2	middle = translator.Reverse(CGAL::midpoint(perimeter.edge(n).source(), perimeter.edge(n).target()));
		road_widths[n] = (gNetEntities[road_types[n]].width)+2.0 + (gNetEntities[road_types[n]].pad);
		road_densities[n] = gNetEntities[road_types[n]].building_percent;
	}

	// Inset by the road width.
	InsetPolygon_2(perimeter, road_widths, 0.5, true, perimeter_inset, NULL, NULL);
	if (perimeter_inset.size() != 4) return false;

	ProcessLotRecursive(inFace, translator, perimeter_inset, road_densities, inDEMs, inMesh);
	return true;
}






/******************************************************************************************************************************
 ******************************************************************************************************************************
 ******************************************************************************************************************************
												PRIMARY INSTANTIATION
 ******************************************************************************************************************************
 ******************************************************************************************************************************
 ******************************************************************************************************************************/

#pragma mark -

void	InstantiateGTPolygon(
							Face_handle					inFace,
							const Polygon_with_holes_2&	inBounds,
							const DEMGeoMap&			dems,
							const CDT&					mesh)
{
	if (inBounds.outer_boundary().is_empty())	return;
	/******************************************************************************************************************
	 * ATTEMPT SUBDIVISION
	 ******************************************************************************************************************/
#if 0
	if (CanUseSubdivision(inFace.first))
	{
		++total_subdiv;
		if (SubdivideFace(inFace.first, dems, mesh))
			return;
	}
#endif
	/******************************************************************************************************************
	 * SETUP
	 ******************************************************************************************************************/

		vector<Polygon2>				local;
		vector<Polygon2>				inner;
		cgal2ben(inBounds, local);

//		Polygon2				border;						// Our border in geo
//		vector<Polygon2>		shape, local, inner;		// Our rings in geo, local, local w/roads
//		vector<vector<int> >	roadTypes;					// road types for each ring
//		vector<vector<double> >	roadWidths;					// roads for each ring
//		vector<vector<double> >	roadDensities;				// urban density per side
		int						i, j;
//		bool					no_road_placement = false;									// Repress road placement of objects
		bool					no_poly_gen = inBounds.number_of_holes() > 0;
		float					shortest_seg = 9e9;
		float					longest_seg = 0.0;
		static int				hint_id = CDT::gen_cache_key();
		int						query[100];
		int						result;

		const DEMGeo&	ele(dems[dem_Elevation]);
		const DEMGeo&	density_dem(dems[dem_UrbanDensity]);
		const DEMGeo&	radial_dem(dems[dem_UrbanRadial]);
	// TODO - fix this
	float poly_area = GetMapFaceAreaMeters(inFace);
	Point2	centroid = cgal2ben(CGAL::centroid(inBounds.outer_boundary().vertices_begin(),inBounds.outer_boundary().vertices_end()));
	float master_heading = RandRange(0, 360);

	// First, compute our outer boundary and general area.
	Bbox2			uber_bounds(dems[dem_Elevation].mWest,dems[dem_Elevation].mSouth,dems[dem_Elevation].mEast,dems[dem_Elevation].mNorth);
	Bbox2			geo_bounds(local.front().bounds());
	CoordTranslator2	mapping;
	mapping.mSrcMin = Point2(geo_bounds.xmin(),geo_bounds.ymin());
	mapping.mSrcMax = Point2(geo_bounds.xmax(),geo_bounds.ymax());

	if (geo_bounds.xmin() == geo_bounds.xmax() ||
		geo_bounds.ymin() == geo_bounds.ymax()) return;


	float	scale = IDEAL_SCALE;	// Meters per pixel, bigger means courser
	double LON_FACTOR;
	double DEG_TO_MTR_LON;
	// Calculate our extent and work out local system in meters.

	{
		double w = mapping.mSrcMax.x() - mapping.mSrcMin.x();
		double h = mapping.mSrcMax.y() - mapping.mSrcMin.y();
		mapping.mSrcMin = Point2(mapping.mSrcMin.x() - w / 10.0, mapping.mSrcMin.y() - h / 10.0);
		mapping.mSrcMax = Point2(mapping.mSrcMax.x() + w / 10.0, mapping.mSrcMax.y() + h / 10.0);
		double	error = cos(CGAL::to_double(mapping.mSrcMin.y()) * DEG_TO_RAD) - cos(CGAL::to_double(mapping.mSrcMax.y()) * DEG_TO_RAD);
		error *= DEG_TO_MTR_LAT;
		w = mapping.mSrcMax.x() - mapping.mSrcMin.x();
		h = mapping.mSrcMax.y() - mapping.mSrcMin.y();
		LON_FACTOR = cos(CGAL::to_double(mapping.mSrcMin.y()) * DEG_TO_RAD);
		DEG_TO_MTR_LON = DEG_TO_MTR_LAT * LON_FACTOR;
		h *= DEG_TO_MTR_LAT;
		w *= DEG_TO_MTR_LON;

		if ((CGAL::to_double(w) / scale) > MAX_WIDTH )	scale = ceil(CGAL::to_double(w) / MAX_WIDTH  );
		if ((CGAL::to_double(h) / scale) > MAX_HEIGHT)	scale = ceil(CGAL::to_double(h) / MAX_HEIGHT );

		w /= scale;
		h /= scale;
		if (ceil(CGAL::to_double(w)) > MAX_WIDTH || ceil(CGAL::to_double(h)) > MAX_HEIGHT)
			AssertPrintf("ERROR: overflow on rasterizing poly!\n");

		mapping.mDstMin = Point2(0,0);
		mapping.mDstMax = Point2(w, h);
		int	width = ceil(CGAL::to_double(mapping.mDstMax.x()));
		width = ((width + 31) / 32) * 32;
		DebugAssert(mapping.mDstMax.x() <= width);		// IMPORTANT - DO NOT WRITE BACK OUR 32-BIT ALIGNED WIDTH TO THE MAPPER!  THIS WOULD BE BAD!
//		mapping.mDstMax.x() = width;						// LOTS OF CODE ASSUMES A REAL RELATIONSHIP TO METERS!  IT'S OKAY TO HAVE EXTRA SLOP ON THE RIGHT

		gImage.ClearBand(0,ceil(CGAL::to_double(h)));
		gImage.mXLimit = ceil(CGAL::to_double(mapping.mDstMax.x()));
		gImage.mYLimit = ceil(CGAL::to_double(mapping.mDstMax.y()));
	}


	// Translate shape to local coords.
	for(int i = 0; i < local.size(); ++i)
	for(int j = 0; j < local[i].size(); ++j)
		local[i][j] = mapping.Forward(local[i][j]);

	if (!no_poly_gen)
	for (j = 0; j < local.front().size(); ++j)
	{
		float len = sqrt(local.front().side(j).squared_length());
		shortest_seg = min(shortest_seg, len);
		longest_seg = max(longest_seg, len);
	}

	Segment2 longest_local((local.front().side(0)));
	for (i = 1; i < local.front().size(); ++i)
	{
		if (longest_local.squared_length() < local.front().side(i).squared_length())
			longest_local = (local.front().side(i));
	}


	// Rasterize our non-area to protect it.
	inner = local;

	inner.push_back(Polygon2());
	inner.back().push_back(cgal2ben(Point_2(mapping.mDstMin.x(),mapping.mDstMin.y())));
	inner.back().push_back(cgal2ben(Point_2(mapping.mDstMin.x(),mapping.mDstMax.y())));
	inner.back().push_back(cgal2ben(Point_2(mapping.mDstMax.x(),mapping.mDstMax.y())));
	inner.back().push_back(cgal2ben(Point_2(mapping.mDstMax.x(),mapping.mDstMin.y())));
	gImage.RasterizeLocal(inner);
	inner.pop_back();

	Vector2 facing(longest_local.p1, longest_local.p2);
	facing.normalize();
	facing = facing.perpendicular_cw();
	master_heading = atan2(facing.dx, facing.dy) * RAD_TO_DEG;

	if (inner.size() > 8) no_poly_gen = true;

//	gImage.Debug();

	/************ INSERT POLYGONAL OBJECTS **********/

		vector<Polygon2>	polyObjLocalV(1);

	for (GISPolyObjPlacementVector::iterator polyObj = inFace->data().mPolyObjs.begin();
		polyObj != inFace->data().mPolyObjs.end(); ++polyObj)
	{
		polyObjLocalV[0].clear();

		for (Polygon_2::iterator vert = polyObj->mShape.outer_boundary().vertices_begin(); vert != polyObj->mShape.outer_boundary().vertices_end(); ++vert)
		{
			polyObjLocalV[0].push_back((mapping.Forward(cgal2ben(*vert))));
		}
		gImage.RasterizeLocal(polyObjLocalV);
	}

	/******************************************************************************************************************
	 * INSERT EXISTING POINT OBJECTS
	 ******************************************************************************************************************/

	for (GISObjPlacementVector::iterator obj = inFace->data().mObjs.begin();
		obj != inFace->data().mObjs.end(); ++obj)
	{
		polyObjLocalV.resize(1);

		Point2	center = (mapping.Forward(cgal2ben(obj->mLocation)));

		if (FetchObjectBoundaryByRep(obj->mRepType, &polyObjLocalV[0], 1.0 / scale, NULL, NULL))
		{
			RotateAndOffset(polyObjLocalV[0], Vector2(center), obj->mHeading);
			gImage.RasterizeLocal(polyObjLocalV);
		}
	}

	/******************************************************************************************************************
	 * CONVERT AREA FEATURES
	 ******************************************************************************************************************/

#if 0
	vector<int>		featuresFromArea;

	if (inFace->mAreaFeature.mFeatType != NO_VALUE)
	{
		no_road_placement = true;
		pair<FeatureToRepTable::iterator,FeatureToRepTable::iterator> range = gFeatureToRep.equal_range(inFace->mAreaFeature.mFeatType);
		for (FeatureToRepTable::iterator i = range.first; i != range.second; ++i)
		{
			float prob = i->second.area_density * poly_area / 1000000.0;
			int num_to_place = prob;
			if (RollDice(prob - (float) num_to_place)) ++num_to_place;
			if (num_to_place > i->second.area_max) num_to_place = i->second.area_max;
			while (num_to_place--)
				featuresFromArea.push_back(i->second.rep_type);
		}
	}
	polyObjLocalV.resize(1);
	polyObjLocalV[0].resize(4);
	RandomizeVector(featuresFromArea);
	for (int n = 0; n < featuresFromArea.size(); ++n)
	{
		Point2	trial = Point2(rand() % (int) mapping.mDstMax.x	, rand() % (int) mapping.mDstMax.y);
		Point2 l = mapping.Reverse(trial);
		if (FetchObjectBoundaryByRep(featuresFromArea[n],&polyObjLocalV[0], 1.0 / scale, NULL, NULL))
		{
			float heading = rand() % 360;
			RotateAndOffset(polyObjLocalV[0], Vector2(trial), heading);
			++feat_raster_try;
			if (!gImage.RasterizeLocalStopConflicts(polyObjLocalV))
			{
				++feat_raster_ok;
				no_poly_gen = true;
				GISObjPlacement_t	obj;
				obj.mRepType = featuresFromArea[n];
				obj.mLocation = l;
				obj.mHeading = heading;
				obj.mDerived = true;
				inFace->mObjs.push_back(obj);
				IncrementRepUsage(obj.mRepType);
				break;
			}
		}
	}
#endif

	/******************************************************************************************************************
	 * CONVERT POLYGON FEATURES
	 ******************************************************************************************************************/

	// NOTE: polygon featuers are not yet imported, so we don't support encoding them yet.
	if (!inFace->data().mPolygonFeatures.empty())
		fprintf(stderr, "WARNING: Polygon features ARE present.\n");

	/******************************************************************************************************************
	 * INSERT POINT FEATURES
	 ******************************************************************************************************************/

		Point2	trial, l;

	for (GISPointFeatureVector::iterator iter = inFace->data().mPointFeatures.begin(); iter != inFace->data().mPointFeatures.end(); ++iter)
	if (IsWellKnownFeature(iter->mFeatType))
	{

		int	terrain = NO_VALUE;

		CDT::Locate_type lt;
		int side;
		CDT::Face_handle recent = NULL;
		recent = mesh.locate(CDT::Point(iter->mLocation.x(),iter->mLocation.y()), lt, side);
		if (lt == CDT::FACE || lt == CDT::EDGE || lt == CDT::VERTEX)
		{
			terrain = highest_prio_tri(recent);
		} else {
			DebugAssert(!"Hrm....object not found on terrain mesh...");
		}

		int		require_feat = iter->mFeatType;
		int		height_max = 0.0;
			 if (iter->mParams.count(pf_Height))		height_max = iter->mParams[pf_Height];
		else if (inFace->data().mParams.count(af_Height))	height_max = inFace->data().mParams[af_Height];

		result = no_poly_gen ? 0 : QueryUsableFacsBySize(					// This is facade placement for specific point features in the
											require_feat, 					// non-subdivision case.
											terrain,
											shortest_seg, longest_seg,
											height_max,
											query,
											1);
		bool	got_it = false;

		for (int ri = 0; ri < result; ++ri)
		{
			RepInfo_t& info = gRepTable[query[ri]];
			{
				polyObjLocalV.resize(1);
//				InsetPolygon2(inner[0], NULL, 1.0 / scale, true, polyObjLocalV[0], NULL, NULL);
				Polygon_2	i;
				Polygon_set_2 polv;
				ben2cgal(inner[0], i);
				BufferPolygon(i,NULL,1.0 / scale, polv);
				vector<Polygon_with_holes_2>	p;
				polv.polygons_with_holes(back_insert_iterator<vector<Polygon_with_holes_2> >(p));
				if(!p.empty())
				{
					cgal2ben(p[0].outer_boundary(),polyObjLocalV[0]);
					++feat_raster_try;
					if (!gImage.RasterizeLocalStopConflicts(polyObjLocalV))
					{
						++feat_raster_ok;
						GISPolyObjPlacement_t	rep;
	//					rep.mShape.push_back(Polygon_with_holes_2());
						rep.mRepType = info.obj_name;
						for (int n = 0; n < polyObjLocalV[0].size(); ++n)
						{
							Point_2 m = ben2cgal(mapping.Reverse(polyObjLocalV[0][n]));
							rep.mShape.outer_boundary().push_back(m);
						}
						if (iter->mParams.find(pf_Height) != iter->mParams.end())
							rep.mHeight = iter->mParams[pf_Height];
						else
							rep.mHeight = RandRange(info.height_min,info.height_max);
						rep.mDerived = true;
						if (SanityCheck(rep))
							inFace->data().mPolyObjs.push_back(rep);
						IncrementRepUsage(rep.mRepType);
						got_it = true;
						break;
					}
				}
			}

		}


//		if we can place polys and we have a rep type that allows for facades and the area matches up, plop down a facade
//		otherwise try to put the rep down on the nearest road.

		// OPTIMIZE: we don't know how big of a space we have, but if we fail on a small object, bigger ones are NOT going to work.
		// (But wait, do we go biggest to smalllest or smallest to biggest here?)

		result = got_it ? 0 : QueryUsableObjsBySize(			// This is obj selection for a point feature basically in the middle of the lot.
											require_feat,
											terrain,
											-1,					// We are placing off in the middle of the lot.  We do not know our size.
											-1,					// Take all objs and try 'em.  (Slow!)
											height_max,
											0, 0,
											query,
											sizeof(query) / sizeof(query[0]));

		if (!got_it)
		for (int ri = 0; ri < result; ++ri)
		{
			RepInfo_t& info = gRepTable[query[ri]];

			polyObjLocalV.resize(1);
			polyObjLocalV[0].resize(4);
			polyObjLocalV[0][0].x_ = -info.width_min * 0.5 / scale;		polyObjLocalV[0][0].y_ = -info.depth_min * 0.5 / scale;
			polyObjLocalV[0][1].x_ = -info.width_min * 0.5 / scale;		polyObjLocalV[0][1].y_ =  info.depth_min * 0.5 / scale;
			polyObjLocalV[0][2].x_ =  info.width_min * 0.5 / scale;		polyObjLocalV[0][2].y_ =  info.depth_min * 0.5 / scale;
			polyObjLocalV[0][3].x_ =  info.width_min * 0.5 / scale;		polyObjLocalV[0][3].y_ = -info.depth_min * 0.5 / scale;

			++feat_raster_try;
			trial = mapping.Forward(cgal2ben(iter->mLocation));
			Vector2 facing(longest_local.p1, longest_local.p2);
			facing.normalize();
			facing = facing.perpendicular_cw();
			float heading = atan2(facing.dx, facing.dy) * RAD_TO_DEG;
			RotateAndOffset(polyObjLocalV[0], Vector2(trial), heading);
			if (!gImage.RasterizeLocalStopConflicts(polyObjLocalV))
			{
				++feat_raster_ok;
				GISObjPlacement_t	rep;
				rep.mRepType = info.obj_name;
				rep.mLocation = iter->mLocation;
				rep.mHeading = heading;
				rep.mDerived = true;
				inFace->data().mObjs.push_back(rep);
				//DebugAssert(uber_bounds.contains(rep.mLocation));
				IncrementRepUsage(rep.mRepType);
				got_it = true;
				break;
			}
		}

		if (!got_it)
		{
			trial = mapping.Forward(cgal2ben(iter->mLocation));

			Vector2 move_it(trial, mapping.Forward(centroid));
			double to_centroid = sqrt(CGAL::to_double(move_it.squared_length()));
			to_centroid = min(to_centroid, MAX_MOVE_FEATURE);
			move_it.normalize();
			move_it = move_it * to_centroid;
			trial = trial + move_it;


			for (int ri = 0; ri < result; ++ri)
			{
				RepInfo_t& info = gRepTable[query[ri]];

				polyObjLocalV.resize(1);
				polyObjLocalV[0].resize(4);
				polyObjLocalV[0][0].x_ = -info.width_min * 0.5 / scale;		polyObjLocalV[0][0].y_ = -info.depth_min * 0.5 / scale;
				polyObjLocalV[0][1].x_ = -info.width_min * 0.5 / scale;		polyObjLocalV[0][1].y_ =  info.depth_min * 0.5 / scale;
				polyObjLocalV[0][2].x_ =  info.width_min * 0.5 / scale;		polyObjLocalV[0][2].y_ =  info.depth_min * 0.5 / scale;
				polyObjLocalV[0][3].x_ =  info.width_min * 0.5 / scale;		polyObjLocalV[0][3].y_ = -info.depth_min * 0.5 / scale;

				++feat_raster_try;
				Vector2 facing(longest_local.p1, longest_local.p2);
				facing.normalize();
				facing = facing.perpendicular_cw();
				float heading = atan2(facing.dx, facing.dy) * RAD_TO_DEG;
				RotateAndOffset(polyObjLocalV[0], Vector2(trial), heading);
				if (!gImage.RasterizeLocalStopConflicts(polyObjLocalV))
				{
					++feat_raster_ok;
					GISObjPlacement_t	rep;
					rep.mRepType = info.obj_name;
					Point_2 m = ben2cgal(mapping.Reverse(trial));
					rep.mLocation = m;
					//DebugAssert(uber_bounds.contains(rep.mLocation));
					rep.mHeading = heading;
					rep.mDerived = true;
					inFace->data().mObjs.push_back(rep);
					IncrementRepUsage(rep.mRepType);
					got_it = true;
					break;
				}
			}
		}
	}

	/******************************************************************************************************************
	 * INSERT POLYGON REPS IF WE CAN
	 ******************************************************************************************************************/

		// poly_area = area of polygon
		// shortest_seg and longest_seg = length of various segments
		// no_road_placement = whether we need to prohibit road placement due to a big area feature being dropped in.
		// no_poly_gen = we can't put a poly down because of pt features

	// If we can do a poly area	placement
	// accumulate the general area params
	// accum a list of all possible poly reps
	// make some decisions and place them


	/******************************************************************************************************************
	 * INSERT ROAD-ANCHORED BUILDINGS
	 ******************************************************************************************************************/

		float	height_lim;
		double	len;
//		int		i, j;
		float 	heading;
		float	density;
		int 	types[500];
		float 	widths[500];
		float 	depths[500];
		double	max_width_slope, max_depth_slope;

		int terrain = NO_VALUE;
		CDT::Locate_type lt;
		CDT::Face_handle recent = NULL;
		int side;

//	if (!no_road_placement)
	{
		height_lim = inFace->data().mParams.count(af_Height) ? inFace->data().mParams[af_Height] : 0;
		polyObjLocalV.resize(1);
		polyObjLocalV[0].resize(4);

		for (i = 0; i < inner.size(); ++i)
		{
			multimap<float, int, greater<float> >	side_lens;
			for (j = 0; j < inner[i].size(); ++j)
				side_lens.insert(multimap<float, int, greater<float> >::value_type(
					inner[i].side(j).squared_length(), j));

			for (multimap<float, int, greater<float> >::iterator iter = side_lens.begin(); iter != side_lens.end(); ++iter)
			{
				j = iter->second;
//				if (roadDensities[i][j] > 0.0)
				{
					Segment2	seg(inner[i].side(j));
					Vector2		along(seg.p1, seg.p2);				// Points along the street
					Vector2		inset(along.perpendicular_ccw());	// Points into the center from the street
					Vector2		facing(along.perpendicular_cw());	// Paints out toward the street
					len = sqrt(seg.squared_length());

					if (len == 0.0 || seg.p1 == seg.p2)
						break;
					if (len > (2.0 * 111120.0))
					{
						printf("ERROR - side too long, we are corrupt! - %f\n", len);
						break;
					}
					inset.normalize();
					along.normalize();
					facing.normalize();
					heading = atan2(facing.dx, facing.dy) * RAD_TO_DEG;

					float t = 0.0;
					while (t < 1.0)
					{
						trial = seg.midpoint(t) + inset;
						l = mapping.Reverse(trial);
						float	spacing = 10.0;

						if (geo_bounds.contains(l))
						{

							terrain = NO_VALUE;
							recent = CDT::Face_handle();
							recent = mesh.locate(CDT::Point(l.x(),l.y()), lt, side);
							if (lt == CDT::FACE || lt == CDT::EDGE || lt == CDT::VERTEX)
							{
								terrain = highest_prio_tri(recent);
							} else {
								DebugAssert(!"Hrm....object not found on terrain mesh...");
							}
//							FindSlopeLimits(recent->info().normal, facing.dx, facing.dy, MAX_VERTICAL_ERROR_BURY, MAX_VERTICAL_ERROR_FLOAT, max_width_slope, max_depth_slope);
//							max_width_slope = min(max_width_slope, len * 3);	// fudge factor for concave areas
							max_width_slope = len * 3;
							max_depth_slope = -1;


							density = density_dem.value_linear(l.x(),l.y());
							density = 0.5 + 0.5 * (min(1.0f, max(0.0f, density)));
							spacing =  10.0 * 2.0 * (pow(0.2f, density));	//	was 1.5 insteaed of 10 - way too dense!  and slow!
							if (RollDice(density))
							{
								result = QueryUsableObjsBySize(			// This is the object selection case along a road for generated objs.
													NO_VALUE, 			// We do know our max width but we do not know our depth.
													terrain,
													max_width_slope,
													max_depth_slope,
													height_lim,
													1, 0,
													query,
													sizeof(query) / sizeof(query[0]));

								float	smallest_width = spacing;

								int 	ok = -1;

								// Object trial loop - does anyone fit here?
//								for (BinaryIterator on(result); on(); ++on)
								for (int on = 0; on < result; ++on)
								{
									widths[on] = gRepTable[query[on]].width_min;
									depths[on] = gRepTable[query[on]].depth_min;
									types [on] = gRepTable[query[on]].obj_name;
									// Fast bail fail case - skip any object bigger than our smallest failure.

									// Generate the actual footprint anad try to rasterize it.
									polyObjLocalV[0][0].x_ = -widths[on] * 0.5 / scale;		polyObjLocalV[0][0].y_ = -depths[on] * 0.5 / scale;
									polyObjLocalV[0][1].x_ = -widths[on] * 0.5 / scale;		polyObjLocalV[0][1].y_ =  depths[on] * 0.5 / scale;
									polyObjLocalV[0][2].x_ =  widths[on] * 0.5 / scale;		polyObjLocalV[0][2].y_ =  depths[on] * 0.5 / scale;
									polyObjLocalV[0][3].x_ =  widths[on] * 0.5 / scale;		polyObjLocalV[0][3].y_ = -depths[on] * 0.5 / scale;

									trial = seg.midpoint(remap_tval(t)) + along*(widths[on]*0.5 / scale) + inset*(depths[on]*0.5 / scale + 1);	// Offset fudge factor
									l = mapping.Reverse(trial);
									RotateAndOffset(polyObjLocalV[0], Vector2(trial), heading);

									++fill_raster_try;
									if (VerticalOkay(polyObjLocalV, l, mapping, ele) && !gImage.RasterizeLocalCheck(polyObjLocalV))
									{
										ok = on;
										break;
									} else
										smallest_width = min(smallest_width, widths[on]);
								}

								if (ok != -1)
								{
									// Object Success case - we have succeeded in placing an object!
									gImage.RasterizeLocal(polyObjLocalV);

									++fill_raster_ok;
									GISObjPlacement_t	obj;
									obj.mRepType = types[ok];
									obj.mLocation = Point_2(l.x(), l.y());
									obj.mHeading = heading;
									obj.mDerived = false;
									inFace->data().mObjs.push_back(obj);
									//DebugAssert(uber_bounds.contains(obj.mLocation));
									IncrementRepUsage(obj.mRepType);
									// If we succeeded, skip past our object, fer cryin' out loud
									if (spacing < (widths[ok]/scale+1))
										spacing = widths[ok]/scale+1;
//									gImage.Debug();

								} else {
									// If we failed and we had something, we can skip our spacing at leaset as far as the smallest failed attempt...
									// we know that nothing's going in there.
									spacing = max(spacing, smallest_width);
								}
							}
						}
						t += spacing / len;
					}
		//			gImage.Debug();
				}
			}
		}
//		gImage.Debug();
	}

	/******************************************************************************************************************
	 * VEGETATION AND INTERIOR RANDOM OBJECTS
	 ******************************************************************************************************************/

	density = max(min(1.0f, density_dem.value_linear(centroid.x(),centroid.y())), 0.0f);
	double radial = max(min(1.0f, radial_dem.value_linear(centroid.x(),centroid.y())), 0.0f);
	int max_tries = (0.5 + 0.5 * density * radial) * 16 * (min(MAX_AREA_FOR_RANDOM,poly_area)) / 62500 ;
	int stop_huge = max_tries / 3;

	for (int tries = 0; tries < max_tries; ++tries)
	{
		height_lim = inFace->data().mParams.count(af_Height) ? inFace->data().mParams[af_Height] : 0;
		polyObjLocalV.resize(1);
		polyObjLocalV[0].resize(4);

		terrain = NO_VALUE;
		 recent = CDT::Face_handle();

		trial = Point2(RandRange(mapping.mDstMin.x(), mapping.mDstMax.x()),
					  RandRange(mapping.mDstMin.y(), mapping.mDstMax.y()));
		l = mapping.Reverse(trial);

		if (!local.front().inside(trial))
			continue;

		if (!geo_bounds.contains(l))
			continue;

		recent = mesh.locate(CDT::Point(l.x(),l.y()), lt, side);
		if (lt == CDT::FACE || lt == CDT::EDGE || lt == CDT::VERTEX)
		{
			terrain = highest_prio_tri(recent);
		} else {
			DebugAssert(!"Hrm....object not found on terrain mesh...");
		}

		Vector3	tnormal(recent->info().normal[0],recent->info().normal[1],recent->info().normal[2]);
		if (tnormal.dz > 0.98)
		{
			heading = master_heading; // RandRange(0, 360);
//			FindSlopeLimits(recent->info().normal, sin(heading * DEG_TO_RAD), cos(heading * DEG_TO_RAD), MAX_VERTICAL_ERROR_BURY, MAX_VERTICAL_ERROR_FLOAT, max_width_slope, max_depth_slope);

			int closest_road = -1;
			double best;
			for (i = 0; i < inner[0].size(); ++i)
			{
				double dist = Segment2(trial,inner[0].side(i).projection(trial)).squared_length();
				if (closest_road == -1 || dist < best && inner[0].side(i).on_left_side(trial))
				{
					closest_road = i;
					best = dist;
				}
			}
			if (closest_road != -1)
			{
				Segment2 s(inner[0].side(closest_road));
				facing = Vector2(s.p1,s.p2).perpendicular_cw();
				facing.normalize();
				heading = atan2(facing.dx, facing.dy) * RAD_TO_DEG;
			}

		} else {
			heading = atan2(tnormal.dx, tnormal.dy) * RAD_TO_DEG;
			if (heading < 0.0) heading += 360.0;
//			FindSlopeLimits(recent->info().normal, tnormal.dx, tnormal.dy, MAX_VERTICAL_ERROR_BURY, MAX_VERTICAL_ERROR_FLOAT, max_width_slope, max_depth_slope);
		}
		max_width_slope = max_depth_slope = -1;

		{
			result = QueryUsableObjsBySize(			// This is the object selection case along a road for generated objs.
								NO_VALUE, 			// We do know our max width but we do not know our depth.
								terrain,
								max_width_slope,
								max_depth_slope,
								height_lim,
								0, 1,
								query,
								sizeof(query) / sizeof(query[0]));

			// We try to have some smarts about not rasterizing objects multiple times
			// when we already know the results - a few floating point compares is a LOT faster than rasterization.
			int 	ok = -1;

			// Object trial loop - does anyone fit here?
//								for (BinaryIterator on(result); on(); ++on)
			for (int on = 0; on < result; ++on)
			{
				if (tries < stop_huge && on > (result / 4))	break;

				widths[on] = gRepTable[query[on]].width_min;
				depths[on] = gRepTable[query[on]].depth_min;
				types [on] = gRepTable[query[on]].obj_name;

				// Generate the actual footprint anad try to rasterize it.
				polyObjLocalV[0][0].x_ = -widths[on] * 0.5 / scale;		polyObjLocalV[0][0].y_ = -depths[on] * 0.5 / scale;
				polyObjLocalV[0][1].x_ = -widths[on] * 0.5 / scale;		polyObjLocalV[0][1].y_ =  depths[on] * 0.5 / scale;
				polyObjLocalV[0][2].x_ =  widths[on] * 0.5 / scale;		polyObjLocalV[0][2].y_ =  depths[on] * 0.5 / scale;
				polyObjLocalV[0][3].x_ =  widths[on] * 0.5 / scale;		polyObjLocalV[0][3].y_ = -depths[on] * 0.5 / scale;

				l = mapping.Reverse(trial);
				RotateAndOffset(polyObjLocalV[0], Vector2(trial), heading);

				++fill_raster_try;
				if (VerticalOkay(polyObjLocalV, l, mapping, ele) && !gImage.RasterizeLocalCheck(polyObjLocalV))
				{
					gImage.RasterizeLocal(polyObjLocalV);

					++fill_raster_ok;
					GISObjPlacement_t	obj;
					obj.mRepType = types[on];
					obj.mLocation = Point_2(l.x(), l.y());
					obj.mHeading = heading;
					obj.mDerived = false;
					inFace->data().mObjs.push_back(obj);
					//DebugAssert(uber_bounds.contains(obj.mLocation));
					IncrementRepUsage(obj.mRepType);
					break;

				}
			}
		}
	}
}







/******************************************************************************************************************************
 ******************************************************************************************************************************
 ******************************************************************************************************************************
 											OTHER PUBLIC FUNCTIONS
 ******************************************************************************************************************************
 ******************************************************************************************************************************
 ******************************************************************************************************************************/

#pragma mark -


void	DumpPlacementCounts(void)
{
	FILE * fi = stdout;
//	FILE * fi = fopen("obj_hist.txt", "a");
//	if (fi == NULL) return;
	int t = 0;
	int k = 0;
	multimap<int, int>	items;
	for (RepUsageTable::iterator iter = gRepUsage.begin(); iter != gRepUsage.end(); ++iter)
	{
		k++;
		t += iter->second;
		items.insert(multimap<int,int>::value_type(iter->second, iter->first));
	}
	for (multimap<int,int>::iterator i = items.begin(); i != items.end(); ++i)
	{
		float act_freq = (float) i->first / (float) t;
		fprintf(fi,"%30s:%07d %5.2lf\n",
				FetchTokenString(i->second),
				i->first,
				100.0 * act_freq);
//				gRepTable[gRepFeatureIndex[i->second]].max_num,
//				100.0 * gRepTable[gRepFeatureIndex[i->second]].freq);
	}

	fprintf(fi, "%d subdivided.\n", total_subdiv);
	fprintf(fi, "Total: %d, Total kinds: %d\n", t, k);
	printf("FEATURES: try: %d ok: %d ratio: %f.\n", feat_raster_try, feat_raster_ok, (float) feat_raster_ok / (float) feat_raster_try);
	printf("FILL    : try: %d ok: %d ratio: %f.\n", fill_raster_try, fill_raster_ok, (float) fill_raster_ok / (float) fill_raster_try);
//	fclose(fi);
}

void	RemoveDuplicates(Pmwx::Face_iterator inFace)
{
	int i, j;
	for (i = 0; i < inFace->data().mPointFeatures.size(); ++i)
	{
		for (j = i + 1; j < inFace->data().mPointFeatures.size(); ++j)
		{
			double dist = LonLatDistMeters(
						    CGAL::to_double(inFace->data().mPointFeatures[i].mLocation.x()),
							CGAL::to_double(inFace->data().mPointFeatures[i].mLocation.y()),
							CGAL::to_double(inFace->data().mPointFeatures[j].mLocation.x()),
							CGAL::to_double(inFace->data().mPointFeatures[j].mLocation.y()));
			if (dist < OVERLAP_MIN)
			{
#if LOG_REMOVE_FEATURES
				FILE * f = fopen("removed_features.txt", "a");
#endif
				if (LowerPriorityFeature(inFace->data().mPointFeatures[i],inFace->data().mPointFeatures[j]))
				{
#if LOG_REMOVE_FEATURES
					if(f) fprintf(f, "Removing %d:%s (%d) for %d:%s (%d) - dist was %lf\n",
											i,FetchTokenString(inFace->mPointFeatures[i].mFeatType),
											GetPossibleFeatureHeight(inFace->mPointFeatures[i]),
											j,FetchTokenString(inFace->mPointFeatures[j].mFeatType),
											GetPossibleFeatureHeight(inFace->mPointFeatures[j]), dist);
#endif
					inFace->data().mPointFeatures.erase(inFace->data().mPointFeatures.begin()+i);
					// Start this row over again because index i is now differnet.
					// j will be i+1
					j = i;
				} else {
#if LOG_REMOVE_FEATURES
					if(f) fprintf(f, "Removing %d:%s (%d) for %d:%s (%d) - dist was %lf\n",
											i,FetchTokenString(inFace->mPointFeatures[j].mFeatType),
											GetPossibleFeatureHeight(inFace->mPointFeatures[j]),
											j,FetchTokenString(inFace->mPointFeatures[i].mFeatType),
											GetPossibleFeatureHeight(inFace->mPointFeatures[i]), dist);
#endif
					inFace->data().mPointFeatures.erase(inFace->data().mPointFeatures.begin()+j);
					--j;
				}
#if LOG_REMOVE_FEATURES
				if (f) fclose(f);
#endif
			}
		}
	}
}

void	RemoveDuplicatesAll(
							Pmwx&				ioMap,
							ProgressFunc		inProg)
{
	int ctr = 0;
	PROGRESS_START(inProg, 0, 1, "Removing duplicate objects...")
	for (Pmwx::Face_iterator f = ioMap.faces_begin(); f != ioMap.faces_end(); ++f, ++ctr)
	if (!f->data().IsWater() && !f->is_unbounded())
	{
		PROGRESS_CHECK(inProg, 0, 1, "Removing duplicate objects...", ctr, ioMap.number_of_faces(), 1000)
		RemoveDuplicates(f);
	}
	PROGRESS_DONE(inProg, 0, 1, "Removing duplicate objects...")
}


void	InstantiateGTPolygonAll(
							const vector<PreinsetFace>& inFaces,
							const DEMGeoMap& 	inDEMs,
							const CDT&			inMesh,
							ProgressFunc		inProg)
{
	int ctr = 0;
	PROGRESS_START(inProg, 0, 1, "Instantiating Face Objects...")
	for (vector<PreinsetFace>::const_iterator face = inFaces.begin(); face != inFaces.end(); ++face, ++ctr)
	{
		vector<Polygon_with_holes_2>	all_areas;
		face->second.polygons_with_holes(back_insert_iterator<vector<Polygon_with_holes_2> >(all_areas));
		for (vector<Polygon_with_holes_2>::const_iterator subarea = all_areas.begin(); subarea != all_areas.end(); ++subarea)
		{
			PROGRESS_CHECK(inProg, 0, 1, "Instantiating Face Objects...", ctr, inFaces.size(), 500)
			InstantiateGTPolygon(face->first, *subarea, inDEMs, inMesh);
		}
	}
	PROGRESS_DONE(inProg, 0, 1, "Instantiating Face Objects...")
	printf("Processed %llu faces.\n", (unsigned long long)inFaces.size());
}

double	GetInsetForEdgeMeters(Halfedge_const_handle inEdge)
{
	bool is_edge_of_map = inEdge->face()->is_unbounded() || inEdge->twin()->face()->is_unbounded();
	bool is_coast = inEdge->face()->data().IsWater() != inEdge->twin()->face()->data().IsWater();
	int best_road = WidestRoadTypeForSegment(inEdge);

	double width = (is_coast && !is_edge_of_map) ? 30.0 : 5.0;
	if (best_road != NO_VALUE)
		width = max(width, (double) gNetEntities[best_road].width + gNetEntities[best_road].pad);
	return width;
}

double	GetInsetForEdgeDegs(Halfedge_const_handle inEdge)
{
	return GetInsetForEdgeMeters(inEdge) * MTR_TO_NM * NM_TO_DEG_LAT;
}

#define TERRAIN_GRID 256

inline void SubBucket(double b1, double b2, double s1, double s2, int divs, int& i1, int& i2)
{
	double f1 = (s1 - b1) / (b2 - b1);
	double f2 = (s2 - b1) / (b2 - b1);

	f1 *= (double) divs;
	f2 *= (double) divs;

	i1 = floor(f1);
	i2 = ceil(f2);	// Use Ceil, not floor+1.  This means that if our range ENDS on a bucket edge,	the right bucket is NOT set.

	if (i1 < 0) i1 = 0;
	if (i2 > divs) i2 = divs;
}

void	GenerateInsets(
					Pmwx& 					ioMap,
					CDT&					ioMesh,
					const Bbox2&			inBounds,
					const set<int>&			inTypes,
					bool					inWantFeatures,
					vector<PreinsetFace>&	outInsets,
					ProgressFunc			func)
{
	outInsets.clear();
	outInsets.reserve(ioMap.number_of_faces());	// Ben says - we REALLY do not want to reallocate the maps of pre-inset faces - that would be @#$@#ing slow!
	int ctr = 0;
	int total = ioMap.number_of_faces();
	int step = total / 200;
	int good_poly = 0, bad_poly = 0, skip_poly = 0, complex_poly = 0;

	char	used_types[TERRAIN_GRID][TERRAIN_GRID] = { 0 };
	int ix1, ix2, iy1, iy2, x, y;

	for (CDT::Finite_faces_iterator ffi = ioMesh.finite_faces_begin(); ffi != ioMesh.finite_faces_end(); ++ffi)
	if (inTypes.count(ffi->info().terrain) > 0)
	{
		Bbox_2	me = ffi->vertex(0)->point().bbox();
		me = me + Point_2(ffi->vertex(1)->point()).bbox();
		me = me + Point_2(ffi->vertex(2)->point()).bbox();

		SubBucket(inBounds.xmin(), inBounds.xmax(), me.xmin(), me.xmax(), TERRAIN_GRID, ix1, ix2);
		SubBucket(inBounds.ymin(), inBounds.ymax(), me.ymin(), me.ymax(), TERRAIN_GRID, iy1, iy2);
		//printf(" (%d %d %d %d) ", ix1, ix2, iy1, iy2);
		for (y = iy1; y < iy2; ++y)
			for (x = ix1; x < ix2; ++x) {
			used_types[x][y] = 1;
	}
	} //else
		//printf(" no %s ", FetchTokenString(ffi->info().terrain));


	PROGRESS_START(func, 0, 1, "Generating usable areas")

	for (Pmwx::Face_iterator f = ioMap.faces_begin(); f != ioMap.faces_end(); ++f, ++ctr)
	if (!f->is_unbounded())
	if (f->data().mTerrainType != terrain_Water)
	{
		PROGRESS_CHECK(func, 0, 1, "Generating usable areas", ctr, total, step)
		Polygon_with_holes_2		bounds;
		PolyInset_t					lims;
		Bbox_2						fextent;
		PolygonFromFace(f, bounds, &lims, GetInsetForEdgeDegs, &fextent);

		bool	want_it = false;
		SubBucket(inBounds.xmin(), inBounds.xmax(), fextent.xmin(), fextent.xmax(), TERRAIN_GRID, ix1, ix2);
		SubBucket(inBounds.ymin(), inBounds.ymax(), fextent.ymin(), fextent.ymax(), TERRAIN_GRID, iy1, iy2);

		if(inWantFeatures && !f->data().mPointFeatures.empty()) want_it = true;

		if (!want_it)
		for (y = iy1; y < iy2; ++y)
		for (x = ix1; x < ix2; ++x)
		if (used_types[x][y])
		{
			want_it = true;
			break;
		}

		if (want_it)
		{
			//printf("<");
			Polygon_set_2		region;

			BufferPolygonWithHoles(bounds,&lims,1.0,region);
			outInsets.push_back(PreinsetFace(f, region));
			++good_poly;

		} else
			++skip_poly;
		//printf("o");
	}
	PROGRESS_DONE(func, 0, 1, "Generating usable areas")
	printf("Good polys: %d bad polys: %d, complex polys: %d, ignored polys: %d\n", good_poly, bad_poly, complex_poly, skip_poly);
}


void	GenerateInsets(
					const set<Face_handle>&	inFaces,
					vector<PreinsetFace>&	outInsets,
					ProgressFunc			func)
{
	if (inFaces.size() < 100) func = NULL;
	outInsets.clear();
	outInsets.reserve(inFaces.size());
	int ctr = 0;
	int total = inFaces.size();
	int step = total / 200;
	int good_poly = 0, bad_poly = 0;

	PROGRESS_START(func, 0, 1, "Generating usable areas")

	for (set<Face_handle>::const_iterator f = inFaces.begin(); f != inFaces.end(); ++f)
	if (!(*f)->is_unbounded())
	if ((*f)->data().mTerrainType != terrain_Water)
	{
		PROGRESS_CHECK(func, 0, 1, "Generating usable areas", ctr, total, step)
		Polygon_with_holes_2		bounds;
		PolyInset_t					lims;
		PolygonFromFace(*f, bounds, &lims, GetInsetForEdgeDegs, NULL);

		Polygon_set_2				region;

		BufferPolygonWithHoles(bounds,&lims,1.0,region);
		++good_poly;
		outInsets.push_back(PreinsetFace(*f, region));
	}
	PROGRESS_DONE(func, 0, 1, "Generating usable areas")
	printf("Good polys: %d bad polys: %d\n", good_poly, bad_poly);
}

