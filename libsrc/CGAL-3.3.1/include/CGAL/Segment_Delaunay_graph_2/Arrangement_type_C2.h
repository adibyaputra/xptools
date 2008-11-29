// Copyright (c) 2003,2004,2005,2006  INRIA Sophia-Antipolis (France) and
// Notre Dame University (U.S.A.).  All rights reserved.
//
// This file is part of CGAL (www.cgal.org); you may redistribute it under
// the terms of the Q Public License version 1.0.
// See the file LICENSE.QPL distributed with CGAL.
//
// Licensees holding a valid commercial license may use this file in
// accordance with the commercial license agreement provided with the software.
//
// This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
// WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
//
// $URL: svn+ssh://scm.gforge.inria.fr/svn/cgal/branches/CGAL-3.3-branch/Segment_Delaunay_graph_2/include/CGAL/Segment_Delaunay_graph_2/Arrangement_type_C2.h $
// $Id$
// 
//
// Author(s)     : Menelaos Karavelas <mkaravel@cse.nd.edu>




#ifndef CGAL_SEGMENT_DELAUNAY_GRAPH_2_ARRANGEMENT_TYPE_C2_H
#define CGAL_SEGMENT_DELAUNAY_GRAPH_2_ARRANGEMENT_TYPE_C2_H

#include <CGAL/enum.h>
#include <CGAL/determinant.h>
#include <CGAL/Segment_Delaunay_graph_2/Basic_predicates_C2.h>
#include <CGAL/Segment_Delaunay_graph_2/Are_same_points_C2.h>
#include <CGAL/Segment_Delaunay_graph_2/Are_same_segments_C2.h>
#include <CGAL/Segment_Delaunay_graph_2/Arrangement_enum.h>

#include <iostream>

CGAL_BEGIN_NAMESPACE

CGAL_SEGMENT_DELAUNAY_GRAPH_2_BEGIN_NAMESPACE

//---------------------------------------------------------------------
//---------------------------------------------------------------------

template<class K>
class Arrangement_type_C2
  : public Basic_predicates_C2<K>, public Internal::Arrangement_enum
{
private:
  typedef Internal::Arrangement_enum         Enum;

  typedef Basic_predicates_C2<K>             Base;

  typedef typename Base::Point_2             Point_2;
  typedef typename Base::Segment_2           Segment_2;
  typedef typename Base::Site_2              Site_2;
  typedef typename Base::Line_2              Line_2;

  typedef typename Base::FT                  FT;
  typedef typename Base::RT                  RT;

  typedef typename Base::Orientation         Orientation;
  typedef typename Base::Comparison_result   Comparison_result;
  typedef typename Base::Oriented_side       Oriented_side;
  typedef typename Base::Sign                Sign;

  typedef typename K::Orientation_2          Orientation_2;

  typedef Are_same_points_C2<K>              Are_same_points_2;
  typedef Are_same_segments_C2<K>            Are_same_segments_2;

private:
  Are_same_points_2     same_points;
  Are_same_segments_2   same_segments;

public:
  typedef typename Enum::Arrangement_type    result_type;

private:
  result_type compute_type_C2(const RT& x1, const RT& y1,
			      const RT& x2, const RT& y2,
			      const RT& x3, const RT& y3,
			      const RT& x4, const RT& y4) const
  {
    RT delta = -det2x2_by_formula(x2 - x1, x4 - x3, y2 - y1, y4 - y3);

    Sign s = CGAL::sign( delta );
    if ( s != CGAL::ZERO ) {
      return non_parallel_C2(x1, y1, x2, y2, x3, y3, x4, y4, delta);
    } else {
      return parallel_C2(x1, y1, x2, y2, x3, y3, x4, y4);
    }
  }

  result_type
  non_parallel_C2(const RT& x1, const RT& y1, const RT& x2, const RT& y2,
		  const RT& x3, const RT& y3, const RT& x4, const RT& y4,
		  const RT& D) const
  {
    RT Dt = -det2x2_by_formula(x3 - x1, x4 - x3, y3 - y1, y4 - y3);
    RT Ds = det2x2_by_formula(x2 - x1, x3 - x1, y2 - y1, y3 - y1);

    Sign s_D = CGAL::sign( D );
    Sign s_Dt = CGAL::sign( Dt );
    Sign s_Ds = CGAL::sign( Ds );

    Sign s_tdiff = CGAL::sign(Dt - D);
    Sign s_sdiff = CGAL::sign(Ds - D);

#ifdef CGAL_CFG_NO_OPERATOR_TIMES_FOR_SIGN
    Sign s_t = CGAL::Sign(s_Dt * s_D);
    Sign s_s = CGAL::Sign(s_Ds * s_D);

    Sign s_t_minus_1 = CGAL::Sign(s_tdiff * s_D);
    Sign s_s_minus_1 = CGAL::Sign(s_sdiff * s_D);
#else
    Sign s_t = s_Dt * s_D;
    Sign s_s = s_Ds * s_D;

    Sign s_t_minus_1 = s_tdiff * s_D;
    Sign s_s_minus_1 = s_sdiff * s_D;
#endif

    if ( s_t == CGAL::NEGATIVE || s_t_minus_1 == CGAL::POSITIVE ||
	 s_s == CGAL::NEGATIVE || s_s_minus_1 == CGAL::POSITIVE ) {
      //  t < 0 or t > 1 or s < 0 or s > 1
      return Enum::DISJOINT;
    }

    int it(0), is(0);
    if ( s_t == CGAL::ZERO ) {
      it = 0;
    } else if ( s_t_minus_1 == CGAL::ZERO ) {
      it = 1;
    } else {
      it = 2;
    }
    if ( s_s == CGAL::ZERO ) {
      is = 0;
    } else if ( s_s_minus_1 == CGAL::ZERO ) {
      is = 1;
    } else {
      is = 2;
    }

    if ( it == 0 ) {
      if ( is == 0 ) {
	return Enum::TOUCH_11;
      } else if ( is == 1 ) {
	return Enum::TOUCH_12;
      } else {
	return Enum::TOUCH_INTERIOR_12;
      }
    } else if ( it == 1 ) {
      if ( is == 0 ) {
	return Enum::TOUCH_21;
      } else if ( is == 1 ) {
	return Enum::TOUCH_22;
      } else {
	return Enum::TOUCH_INTERIOR_22;
      }
    } else {
      if ( is == 0 ) {
	return Enum::TOUCH_INTERIOR_11;
      } else if ( is == 1 ) {
	return Enum::TOUCH_INTERIOR_21;
      } else {
	return Enum::CROSSING;
      }
    }
  }


  result_type
  parallel_C2(const RT& x1, const RT& y1, const RT& x2, const RT& y2,
	      const RT& x3, const RT& y3, const RT& x4, const RT& y4) const
  {
    RT D1 = det2x2_by_formula(x2 - x1, x3 - x1,	y2 - y1, y3 - y1);

    if ( CGAL::sign( D1 ) != CGAL::ZERO ) {
      return Enum::DISJOINT;
    }

    RT Dt3, Dt4, Dt;
    if ( CGAL::compare(x2, x1) != CGAL::EQUAL ) {
      Dt  = x2 - x1;
      Dt3 = x3 - x1;
      Dt4 = x4 - x1;
    } else {
      Dt  = y2 - y1;
      Dt3 = y3 - y1;
      Dt4 = y4 - y1;
    }

    Sign s_Dt = CGAL::sign( Dt );
    Sign s_Dt3 = CGAL::sign( Dt3 );
    Sign s_Dt4 = CGAL::sign( Dt4 );

#ifdef CGAL_CFG_NO_OPERATOR_TIMES_FOR_SIGN
    Sign s_t3 = CGAL::Sign(s_Dt3 * s_Dt);
    Sign s_t4 = CGAL::Sign(s_Dt4 * s_Dt);
#else
    Sign s_t3 = s_Dt3 * s_Dt;
    Sign s_t4 = s_Dt4 * s_Dt;
#endif

    Sign s_t3diff = CGAL::sign( Dt3 - Dt );
    Sign s_t4diff = CGAL::sign( Dt4 - Dt );

#ifdef CGAL_CFG_NO_OPERATOR_TIMES_FOR_SIGN
    Sign s_t3_minus_1 = CGAL::Sign(s_t3diff * s_Dt);
    Sign s_t4_minus_1 = CGAL::Sign(s_t4diff * s_Dt);
#else
    Sign s_t3_minus_1 = s_t3diff * s_Dt;
    Sign s_t4_minus_1 = s_t4diff * s_Dt;
#endif

    int it3(0), it4(0);
    if ( s_t3 == CGAL::ZERO ) { // t3 == 0
      it3 = 0;
    } else if ( s_t3_minus_1 == CGAL::ZERO ) { // t3 == 1
      it3 = 1;
    } else if ( s_t3 == CGAL::POSITIVE &&
		s_t3_minus_1 == CGAL::NEGATIVE ) { // 0 < t3 < 1
      it3 = 2;
    } else if ( s_t3 == CGAL::NEGATIVE ) { // t3 < 0
      it3 = -1;
    } else { // t3 > 1
      it3 = 3;
    }

    if ( s_t4 == CGAL::ZERO ) { // t4 == 0
      it4 = 0;
    } else if ( s_t4_minus_1 == CGAL::ZERO ) { // t4 == 1
      it4 = 1;
    } else if ( s_t4 == CGAL::POSITIVE &&
		s_t4_minus_1 == CGAL::NEGATIVE ) { // 0 < t4 < 1
      it4 = 2;
    } else if ( s_t4 == CGAL::NEGATIVE ) { // t4 < 0
      it4 = -1;
    } else { // t4 > 1
      it4 = 3;
    }

    // decode now
    if ( it3 == -1 ) {
      if ( it4 == -1 ) {
	return Enum::DISJOINT;
      } else if ( it4 == 0 ) {
	return Enum::TOUCH_12;
      } else if ( it4 == 1 ) {
	return Enum::TOUCH_22_INTERIOR_2;
      } else if ( it4 == 2 ) {
	return Enum::OVERLAPPING_12;
      } else { // it4 == 3
	return Enum::INTERIOR_2;
      }
    } else if ( it3 == 0 ) {
      CGAL_assertion( it4 != 0 );
      if ( it4 == -1 ) {
	return Enum::TOUCH_11;
      } else if ( it4 == 1 ) {
	return Enum::IDENTICAL;
      } else if ( it4 == 2 ) {
	return Enum::TOUCH_11_INTERIOR_1;
      } else { // it4 == 3
	return Enum::TOUCH_11_INTERIOR_2;
      }
    } else if ( it3 == 1 ) {
      CGAL_assertion( it4 != 1 );
      if ( it4 == -1 ) {
	return Enum::TOUCH_21_INTERIOR_2;
      } else if ( it4 == 0 ) {
	return Enum::IDENTICAL;
      } else if ( it4 == 2 ) {
	return Enum::TOUCH_21_INTERIOR_1;
      } else { // it4 == 3
	return Enum::TOUCH_21;
      }
    } else if ( it3 == 2 ) {
      if ( it4 == -1 ) {
	return Enum::OVERLAPPING_11;
      } else if ( it4 == 0 ) {
	return Enum::TOUCH_12_INTERIOR_1;
      } else if ( it4 == 1 ) {
	return Enum::TOUCH_22_INTERIOR_1;
      } else if ( it4 == 2 ) {
	return Enum::INTERIOR_1;
      } else { // it4 == 3
	return Enum::OVERLAPPING_21;
      }
    } else { // it3 == 3  ( t3 > 1 )
      if ( it4 == -1 ) {
	return Enum::INTERIOR_2;
      } else if ( it4 == 0 ) {
	return Enum::TOUCH_12_INTERIOR_2;
      } else if ( it4 == 1 ) {
	return Enum::TOUCH_22;
      } else if ( it4 == 2 ) {
	return Enum::OVERLAPPING_22;
      } else { // it4 == 3
	return Enum::DISJOINT;
      }
    }
  }


  bool inside_segment(const Site_2& s, const Site_2& p) const
  {
    CGAL_precondition( s.is_segment() && p.is_point() );

    Line_2 l = compute_supporting_line( s.supporting_site() );
    // do geometric filtering here...

    Point_2 pp = p.point();

    Oriented_side os =  oriented_side_of_line(l, pp );

    if ( os != ON_ORIENTED_BOUNDARY ) {
      // the point does not belong to the same line as the segment
      return false;
    }

    Line_2 lp1 = compute_perpendicular(l, s.segment().source());

    Oriented_side os1 = oriented_side_of_line(lp1, pp);

    CGAL_assertion( os1 != ON_ORIENTED_BOUNDARY );

    if ( os1 == ON_POSITIVE_SIDE ) {
      return false;
    }

    Line_2 lp2 = compute_perpendicular(l, s.segment().target());
    lp2 = opposite_line(lp2);

    Oriented_side os2 = oriented_side_of_line(lp2, pp);

    CGAL_assertion( os2 != ON_ORIENTED_BOUNDARY );

    if ( os2 == ON_POSITIVE_SIDE ) {
      return false;
    }

    return true;
  }

  //------------------------------------------------------------------------

  result_type
  arrangement_type_same_point(const Site_2& p, const Site_2& q,
			      unsigned int ip, unsigned int iq) const
  {
    CGAL_precondition( ip < 2 && iq < 2 );
#if 0
    if ( same_segments(p.supporting_site(), q.supporting_site()) ) {
      Line_2 l = compute_supporting_line(p.supporting_site());
      Line_2 lp;

      if ( ip == 0 ) {
	lp = compute_perpendicular(l, p.segment().source());
      } else {
	lp = compute_perpendicular(l, p.segment().target());
	lp = opposite_line(lp);
      }

      Oriented_side os;

      if ( iq == 0 ) {
	os = oriented_side_of_line(lp, q.segment().target());
      } else {
	os = oriented_side_of_line(lp, q.segment().source());
      }

      CGAL_assertion( os != ON_ORIENTED_BOUNDARY );

      if ( os == ON_POSITIVE_SIDE ) {
	return std::pair<int,int>(ip,iq);
      } else {
	return std::pair<int,int>(5,5);
      }
    }
#endif
    Point_2 p1 = p.supporting_site().source();
    Point_2 p2 = p.supporting_site().target();
    Point_2 p3;

    if ( iq == 0 ) {
      p3 = q.supporting_site().target();
    } else {
      p3 = q.supporting_site().source();
    }

    if ( Orientation_2()(p1, p2, p3) != COLLINEAR ) {
      if ( ip == 0 ) {
	if ( iq == 0 ) {
	  return TOUCH_11;
	} else {
	  return TOUCH_12;
	}
      } else {
	if ( iq == 0 ) {
	  return TOUCH_21;
	} else {
	  return TOUCH_22;
	}
      }

    } else {
      Segment_2 s1 = p.segment();
      Segment_2 s2 = q.segment();

      return parallel_C2( s1.source().x(), s1.source().y(),
			  s1.target().x(), s1.target().y(),
			  s2.source().x(), s2.source().y(),
			  s2.target().x(), s2.target().y() );
    }
  }

  result_type
  arrangement_type_ss(const Site_2& p, const Site_2& q) const
  {
    if ( same_segments(p, q) ) {
      return IDENTICAL;
    }

    if ( same_points(p.source_site(), q.source_site()) ) {
      return arrangement_type_same_point(p, q, 0, 0);
    } if ( same_points(p.source_site(), q.target_site()) ) {
      return arrangement_type_same_point(p, q, 0, 1);
    } else if ( same_points(p.target_site(), q.source_site()) ) {
      return arrangement_type_same_point(p, q, 1, 0);
    } else if ( same_points(p.target_site(), q.target_site()) ) {
      return arrangement_type_same_point(p, q, 1, 1);
    }

    Segment_2 s1 = p.segment();
    Segment_2 s2 = q.segment();

    result_type res = compute_type_C2( s1.source().x(), s1.source().y(),
				       s1.target().x(), s1.target().y(),
				       s2.source().x(), s2.source().y(),
				       s2.target().x(), s2.target().y() );

    return res;
  }

  //--------------------------------------------------------------------

  result_type
  arrangement_type_ps(const Site_2& p, const Site_2& q) const
  {
    if ( same_points(p, q.source_site()) ) {
      return TOUCH_1;
    } else if ( same_points(p, q.target_site()) ) {
      return TOUCH_2;
    } else if ( inside_segment(q, p) ) {
      return INTERIOR;
    } else {
      return DISJOINT;
    }
  }

  //--------------------------------------------------------------------

  result_type
  arrangement_type_pp(const Site_2& p, const Site_2& q) const
  {
    if ( same_points(p, q) ) {
      return IDENTICAL;
    } else {
      return DISJOINT;
    }
  }

  //--------------------------------------------------------------------

public:
  typedef Site_2                   argument_type;
  typedef Arity_tag<2>             Arity;


  result_type
  operator()(const Site_2& p, const Site_2& q) const
  {
    CGAL_precondition( p.is_defined() && q.is_defined() );

    if ( p.is_point() && q.is_point() ) {
      return arrangement_type_pp(p, q);
    } else if ( p.is_point() && q.is_segment() ) {
      return arrangement_type_ps(p, q);
    } else if ( p.is_segment() && q.is_point() ) {
      return opposite( arrangement_type_ps(q, p) );
    } else {
      return arrangement_type_ss(p, q);
    }
  }
};

//---------------------------------------------------------------------

CGAL_SEGMENT_DELAUNAY_GRAPH_2_END_NAMESPACE

CGAL_END_NAMESPACE

#endif // CGAL_SEGMENT_DELAUNAY_GRAPH_2_ARRANGEMENT_TYPE_C2_H