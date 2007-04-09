#include "WED_GISLine_Width.h"

START_CASTING(WED_GISLine_Width)
IMPLEMENTS_INTERFACE(IGISEntity)
IMPLEMENTS_INTERFACE(IGISPointSequence)
IMPLEMENTS_INTERFACE(IGISLine)
IMPLEMENTS_INTERFACE(IGISLine_Width)
INHERITS_FROM(WED_GISLine)
END_CASTING

WED_GISLine_Width::WED_GISLine_Width(WED_Archive * parent, int id) : 
	WED_GISLine(parent, id),
	width(this,"width","GIS_lines_heading", "width", 50.0)
{
}

WED_GISLine_Width::~WED_GISLine_Width()
{
}

GISClass_t		WED_GISLine_Width::GetGISClass		(void				 ) const
{
	return gis_Line_Width;
}

double	WED_GISLine_Width::GetWidth (void		 ) const
{
	return width.value;
}

void	WED_GISLine_Width::SetWidth (double w)
{
	if (w != width.value)
	{
		StateChanged();
		width.value = w;
	}
}