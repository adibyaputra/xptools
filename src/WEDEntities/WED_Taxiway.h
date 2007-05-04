#ifndef WED_TAXIWAY_H
#define WED_TAXIWAY_H

#include "WED_GISPolygon.h"

class	WED_Taxiway : public WED_GISPolygon {

DECLARE_PERSISTENT(WED_Taxiway)

private:

	WED_PropIntEnum			surface;
	WED_PropDoubleText		roughness;
	WED_PropDoubleText		heading;
	
};

#endif /* WED_TAXIWAY_H */