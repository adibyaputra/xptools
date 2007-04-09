#include "WED_MarqueeTool.h"
#include "ISelection.h"
#include "IResolver.h"
#include "AssertUtils.h"
#include "IGIS.h"

//	HANDLES			LINKS
// 2-3-4			+2-3+
// |   |			1	4
// 1   5			|   |
// |   |			0	5
// 0-7-6			+7-6+

// This maps the relative contribution of a box corner to a handle.  So the 0th handle
// is made entirely of the first point (for both X and Y).
static const double kControlsX1[8] = {	1.0, 1.0, 1.0, 0.5, 0.0, 0.0, 0.0, 0.5 };
static const double kControlsX2[8] = {	0.0, 0.0, 0.0, 0.5, 1.0, 1.0, 1.0, 0.5 };
static const double kControlsY1[8] = {	1.0, 0.5, 0.0, 0.0, 0.0, 0.5, 1.0, 1.0 };
static const double kControlsY2[8] = {	0.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.0, 0.0 };

// How much to transform each point given a handle move!
static const double kApplyCtrlX1[8] = { 1.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
static const double kApplyCtrlX2[8] = { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0.0 };
static const double kApplyCtrlY1[8] = { 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0 };
static const double kApplyCtrlY2[8] = { 0.0, 0.0, 1.0, 1.0, 1.0, 0.0, 0.0, 0.0 };

// How much to transform each point when we drag a LINK!
static const double kApplyLinkX1[8] = { 1.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
static const double kApplyLinkX2[8] = { 0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 0.0, 0.0 };
static const double kApplyLinkY1[8] = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 1.0 };
static const double kApplyLinkY2[8] = { 0.0, 0.0, 1.0, 1.0, 0.0, 0.0, 0.0, 0.0 };

WED_MarqueeTool::WED_MarqueeTool(
										WED_MapZoomerNew *		zoomer,
										IResolver *				resolver,
										const char *			root_path,
										const char *			selection_path) :
				WED_HandleToolBase(zoomer, resolver, root_path, selection_path),				
				mResolver(resolver),
				mSelection(selection_path)				
{
	SetControlProvider(this);
}

WED_MarqueeTool::~WED_MarqueeTool()
{
}

int		WED_MarqueeTool::CountEntities(void) const
{
	return 1;
}

int		WED_MarqueeTool::GetNthEntityID(int n) const
{
	return 0;
}

int		WED_MarqueeTool::CountControlHandles(int id						  ) const
{
	return 8;
}

void	WED_MarqueeTool::GetNthControlHandle(int id, int n,		 Point2& p) const
{
	Bbox2	bounds;
	if (!GetTotalBounds(bounds))
	{
		p = Point2(); return;
	}
	
	p.x = bounds.p1.x * kControlsX1[n] + bounds.p2.x * kControlsX2[n];
	p.y = bounds.p1.y * kControlsY1[n] + bounds.p2.y * kControlsY2[n];
}

void	WED_MarqueeTool::SetNthControlHandle(int id, int n, const Point2& p)
{
	Bbox2	bounds;
	if (!GetTotalBounds(bounds)) return;
	Point2 old;
	
	old.x = bounds.p1.x * kControlsX1[n] + bounds.p2.x * kControlsX2[n];
	old.y = bounds.p1.y * kControlsY1[n] + bounds.p2.y * kControlsY2[n];

	this->ControlsHandlesBy(id, n, Vector2(old, p));
}

int		WED_MarqueeTool::GetLinks		    (int id) const
{
	return 8;
}

int		WED_MarqueeTool::GetNthLinkSource   (int id, int n) const
{
	return n;
}

int		WED_MarqueeTool::GetNthLinkSourceCtl(int id, int n) const
{
	return -1;
}

int		WED_MarqueeTool::GetNthLinkTarget   (int id, int n) const
{
	return (n+1)%8;
}

int		WED_MarqueeTool::GetNthLinkTargetCtl(int id, int n) const
{
	return -1;
}

bool	WED_MarqueeTool::PointOnStructure(int id, const Point2& p) const
{
	return false;
}

void	WED_MarqueeTool::ControlsMoveBy(int id, const Vector2& delta)
{
	Bbox2	old_b, new_b;
	if (!GetTotalBounds(old_b)) return;
	new_b = old_b;
	new_b.p1 += delta;
	new_b.p2 += delta;
	ApplyRescale(old_b,new_b);	
}

void	WED_MarqueeTool::ControlsHandlesBy(int id, int c, const Vector2& delta)
{
	Bbox2	old_b, new_b;
	if (!GetTotalBounds(old_b)) return;
	new_b = old_b;
	
	new_b.p1.x += (delta.dx * kApplyCtrlX1[c]);
	new_b.p2.x += (delta.dx * kApplyCtrlX2[c]);
	new_b.p1.y += (delta.dy * kApplyCtrlY1[c]);
	new_b.p2.y += (delta.dy * kApplyCtrlY2[c]);

	ApplyRescale(old_b,new_b);
}

void	WED_MarqueeTool::ControlsLinksBy	 (int id, int c, const Vector2& delta)
{
	Bbox2	old_b, new_b;
	if (!GetTotalBounds(old_b)) return;
	new_b = old_b;
	
	new_b.p1.x += (delta.dx * kApplyLinkX1[c]);
	new_b.p2.x += (delta.dx * kApplyLinkX2[c]);
	new_b.p1.y += (delta.dy * kApplyLinkY1[c]);
	new_b.p2.y += (delta.dy * kApplyLinkY2[c]);

	ApplyRescale(old_b,new_b);
}


void WED_MarqueeTool::GetEntityInternal(vector<IGISEntity *>& e)
{
	ISelection * sel = SAFE_CAST(ISelection,mResolver->Resolver_Find(mSelection.c_str()));
	DebugAssert(sel != NULL);

	vector<IUnknown *>	iu;
	vector<IGISEntity *> en;
	
	e.clear();
	
	sel->GetSelectionVector(iu);
	if (iu.empty()) return;
	en.reserve(iu.size());
	for (vector<IUnknown *>::iterator i = iu.begin(); i != iu.end(); ++i)
	{
		IGISEntity * ent = SAFE_CAST(IGISEntity,*i);
		if (ent) e.push_back(ent);
	}
}

#if !DEV
	hrm - ths is a case where bulk fetch would be more efficient by a factor of, um, 8??
	but - this is a special case.  in most cases the data model can produce answers quickly,
	and having to COPY the handle set sucks.  So probably its better for the  whole app to
	solve this with caching.  Thought: if the sel had generation change numbers, we could
	inval the cache when the sel changes.   We could also just respod to an "any changed" msg.
#endif

bool	WED_MarqueeTool::GetTotalBounds(Bbox2& b) const
{
	b = Bbox2();
	ISelection * sel = SAFE_CAST(ISelection,mResolver->Resolver_Find(mSelection.c_str()));
	DebugAssert(sel != NULL);

	vector<IUnknown *>	iu;
	int ret = false;
	
	sel->GetSelectionVector(iu);
	if (iu.empty()) return false;
	for (vector<IUnknown *>::iterator i = iu.begin(); i != iu.end(); ++i)
	{
		IGISEntity * ent = SAFE_CAST(IGISEntity,*i);
		if (ent)
		{
			Bbox2 local;
			ent->GetBounds(local);
			b += local;
		}
	}	
	return !b.is_null();
}

void	WED_MarqueeTool::ApplyRescale(const Bbox2& old_bounds, const Bbox2& new_bounds)
{
	ISelection * sel = SAFE_CAST(ISelection,mResolver->Resolver_Find(mSelection.c_str()));
	DebugAssert(sel != NULL);

	vector<IUnknown *>	iu;
	
	sel->GetSelectionVector(iu);
	for (vector<IUnknown *>::iterator i = iu.begin(); i != iu.end(); ++i)
	{
		IGISEntity * ent = SAFE_CAST(IGISEntity,*i);
		if (ent)
			ent->Rescale(old_bounds,new_bounds);
	}	

}
