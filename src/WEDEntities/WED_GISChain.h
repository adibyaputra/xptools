#ifndef WED_GISCHAIN_H
#define WED_GISCHAIN_H

#include "WED_Entity.h"
#include "IGIS.h"

class	WED_GISChain : public WED_Entity, public virtual IGISPointSequence {

DECLARE_INTERMEDIATE(WED_GISChain)

public:

	// IGISEntity
	virtual	GISClass_t		GetGISClass		(void				 ) const;
	virtual	void			GetBounds		(	   Bbox2&  bounds) const;
//	virtual	int				IntersectsBox	(const Bbox2&  bounds) const;
	virtual	bool			WithinBox		(const Bbox2&  bounds) const;
	virtual bool			PtWithin		(const Point2& p	 ) const;
	virtual bool			PtOnFrame		(const Point2& p, double d) const;
	virtual	void			Rescale			(const Bbox2& old_bounds,const Bbox2& new_bounds);
	// IGISPointSequence
	virtual	int					GetNumPoints(void ) const;	
//	virtual	void				DeletePoint (int n)		 ;
//	virtual		  IGISPoint *	SplitSide   (int n)		 ;		// Split the side from pt N to pt N + 1 in half. Return the new pt.
	virtual		  IGISPoint *	GetNthPoint (int n) const;	

	virtual	int					GetNumSides(void) const;
	virtual	bool				GetSide(int n, Segment2& s, Bezier2& b) const;	// true for bezier
	

//	virtual	bool				IsClosed(void) const;

};

#endif /* WED_GISCHAIN_H */