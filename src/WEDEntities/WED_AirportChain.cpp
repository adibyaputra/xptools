#include "WED_AirportChain.h"
#include "WED_AirportNode.h"
#include "IODefs.h"
#include "SQLutils.h"
#include "WED_Errors.h"

DEFINE_PERSISTENT(WED_AirportChain)
START_CASTING(WED_AirportChain)
INHERITS_FROM(WED_GISChain)
END_CASTING

WED_AirportChain::WED_AirportChain(WED_Archive * a, int i) : WED_GISChain(a,i),
	closed(0)
{
}

WED_AirportChain::~WED_AirportChain()
{
}

void	WED_AirportChain::SetClosed(int closure)
{
	if (closed != closure)
	{
		StateChanged();
		closed = closure;
	}
}

IGISPoint *	WED_AirportChain::SplitSide   (int n)
{
	int c = GetNumSides();
	
	if (n > c) return NULL;
	
	Bezier2		b;
	Segment2	s;
	
	if (GetSide(n, s, b))
	{
		WED_AirportNode * node = WED_AirportNode::CreateTyped(GetArchive());
		
		Bezier2	b1, b2;
		b.partition(b1,b2,0.5);
		
		node->SetLocation(b2.p1);
		node->SetControlHandleHi(b2.c1);
		
		node->SetParent(this, n+1);		
		return node;

	} else {

		WED_AirportNode * node = WED_AirportNode::CreateTyped(GetArchive());
		
		node->SetLocation(s.midpoint(0.5));
		node->SetControlHandleHi(s.midpoint(0.5));
		
		node->SetParent(this, n+1);
		return node;
	}
	
}

bool	 WED_AirportChain::IsClosed	(void	) const
{
	return closed;
}


void 			WED_AirportChain::ReadFrom(IOReader * reader)
{
	WED_GISChain::ReadFrom(reader);
	reader->ReadInt(closed);
}

void 			WED_AirportChain::WriteTo(IOWriter * writer)
{
	WED_GISChain::WriteTo(writer);
	writer->WriteInt(closed);
}

void			WED_AirportChain::FromDB(sqlite3 * db)
{
	WED_GISChain::FromDB(db);
	
	sql_command	cmd(db,"SELECT closed FROM WED_airportchains WHERE id=@i;","@i");
	
	sql_row1<int>						key(GetID());
	sql_row1<int>						cl;
	
	int err = cmd.simple_exec(key, cl);
	if (err != SQLITE_DONE)	WED_ThrowPrintf("Unable to complete thing query: %d (%s)",err, sqlite3_errmsg(db));
	
	closed = cl.a; 	
}

void			WED_AirportChain::ToDB(sqlite3 * db)
{
	WED_GISChain::ToDB(db);

	int err;
	sql_command	write_me(db,"UPDATE WED_airportchains set closed=@p WHERE id=@id;","@p,@id");
	sql_row2 <int,int>bindings(closed,GetID());
	err = write_me.simple_exec(bindings);
	if(err != SQLITE_DONE)		WED_ThrowPrintf("UNable to update thing info: %d (%s)",err, sqlite3_errmsg(db));	
}
