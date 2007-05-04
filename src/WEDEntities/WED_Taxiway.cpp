#include "WED_Taxiway.h"
#include "WED_EnumSystem.h"

DEFINE_PERSISTENT(WED_Taxiway)
START_CASTING(WED_Taxiway)
INHERITS_FROM(WED_GISPolygon)
END_CASTING

WED_Taxiway::WED_Taxiway(WED_Archive * a, int i) : WED_GISPolygon(a,i),
	surface(this,		"Surface",			"WED_taxiway",	"surface",	Surface_Type,	surf_Concrete),
	roughness(this,		"Roughness",		"WED_taxiway",	"roughness",	0.25),
	heading(this,		"Texture Heading",	"WED_taxiway",	"heading",		0)
{
}

WED_Taxiway::~WED_Taxiway()
{
}
