#ifndef WED_GISCOMPOSITE_H
#define WED_GISCOMPOSITE_H

#include "WED_Entity.h"
#include "IGIS.h"

class	WED_GISComposite : public WED_Entity, public virtual IGISComposite {

DECLARE_INTERMEDIATE(WED_GISComposite)

public:

	// IGISEntity
	virtual	GISClass_t		GetGISClass		(void				 ) const;
	virtual	void			GetBounds		(	   Bbox2&  bounds) const;
//	virtual	int				IntersectsBox	(const Bbox2&  bounds) const;
	virtual	bool			WithinBox		(const Bbox2&  bounds) const;
	virtual bool			PtWithin		(const Point2& p	 ) const;
	virtual bool			PtOnFrame		(const Point2& p, double d) const;
	virtual	void			Rescale(const Bbox2& old_bounds,const Bbox2& new_bounds);
	// IGISComposite
	virtual	int				GetNumEntities(void ) const;
	virtual	IGISEntity *	GetNthEntity  (int n) const;

};

#endif