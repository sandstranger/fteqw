/*
Daikatana's CSplineSequence::MakeCurveSegments and MakeCurveSegment (its client's dk_gce_main.cpp): beams along splines, for its
effects (dk_cl_curve.cpp). The rest of that file is its cinematics'.
*/
#include "client.h"
#include "dk_array.h"
#include "dk_point.h"
#include "dk_gce_spline.h"
#include "dk_beams.h"

beamSeg_t *CSplineSequence::MakeCurveSegment (float time_start, float time_end, int segment_num, beam_t *beam) 
{
	beamSeg_t	*beamSeg;

	beamSeg = beam_AllocSeg ();
	if (beamSeg == NULL)
		return	NULL;

    //get the initial and ending positions of the laser.
    CVector loc, endloc;
    Pos(time_start, loc);
    Pos(time_end, endloc);

    //position the entity.
    beamSeg->segStart = loc;
    beamSeg->segEnd = endloc;

	beamSeg->segNormal = beamSeg->segEnd - beamSeg->segStart;
	beamSeg->segNormal.Normalize();

	return	beamSeg;
}

int CSplineSequence::MakeCurveSegments (float max_angle, float max_length) 
{
	beam_t		*beam;
	beamSeg_t	*beamSeg, *lastSeg = NULL;
	CVector		beamNormal;

	beam = beam_AllocBeam ();
	if (beam == NULL)
		return	0;

	//	set up basic info for the beam
	beam->alpha			= 1.0;
	beam->startRadius	= 1.0;
	beam->endRadius		= 1.0;
	beam->texIndex		= BEAM_TEX_LASER;
	beam->texIndex2		= BEAM_TEX_NONE;
	beam->rgbColor.Set( 1.0, 1.0f, 1.0f );

    //the number of entities we have actually used to make the curve.
    int num_entities = 0;

    //check the parameters passed in.
    bound_min(max_angle, 1.0f);
    bound_max(max_angle, 80.0f);

    bound_min(max_length, 1.0f);

    //square the length.
    max_length = max_length * max_length;

    //compute the cos of the tolerance angle that was passed in.
    float angle_tolerance = cos(max_angle / 180.0f * PI);

    //move along the spline and construct lasers to approximate the path.
    //get the initial velocity direction.
    CVector begin_vel;
    CVector begin_pos;
    float begin_time = 0.0f;
    int begin_seg = 0;

    Pos(begin_time, begin_pos);
    Vel(begin_time, begin_vel);

	beam->beamStart = begin_pos;

    //compute the step length, based on the time of the current segment.
    float step = segment_data.Item(begin_seg).time / 50;
    bound_min(step, 0.01f);

    for (float time = 0.0f; time <= TotalTime(); time += step) {
        //get the position at this time.
        CVector pos;
        Pos(time, pos);

        //get the distance since the last position.
        CVector line;
        line = pos - begin_pos;
        float distance = line.DotProduct(line);

        //check if we have moved far enough to allow a new entity.
        if (distance < 1.0f) {
            //we havent move far enough in the world.
            continue;
        }

        //we use this copy of the current time because our call to Segment below will modify the value
        //we pass it.
        float segment_time = time;

        //get the segment number for the current time.
        int cur_seg = Segment(segment_time);

        //get the velocity vector at this time
        CVector vel;
        Vel(time, vel);

        //check if we should make a laser from the last point to the current point.
        if ((vel.DotProduct(begin_vel) > angle_tolerance) &&   //the spline is still on a straight course.
            (distance < max_length) &&                  //we haven't gone too far since the end of the last piece.
            (cur_seg == begin_seg))                     //we are still on the same segment.
        {
            //we should go farther before making the next laser.
            continue;
        }

        //the angles are far enough apart, make a marker.
        //get the begin and ending time we use to find positions from the spline.
        float time_start = begin_time;
        float time_end = time;
        if (time_end - time_start < 0.005f) {
            time_start = time_end - 0.005f;
        }

        //save the current data for next time.
        begin_pos = pos;
        begin_vel = vel;
        begin_time = time;
        begin_seg = cur_seg;

        //recompute our step.
        step = segment_data.Item(begin_seg).time / 50;
        bound_min(step, 0.01f);

        //	add a segment to the beam
        beamSeg = MakeCurveSegment (time_start, time_end, begin_seg, beam);
		if (beamSeg)
		{
			beam->numSegs++;
			if (lastSeg == NULL)
				//	first segment in beam list, so point list to it
				beam->firstSeg = beamSeg;
			else
				lastSeg->nextSeg = beamSeg;
			lastSeg = beamSeg;
		}

		num_entities++;
    }

    //get the end point of the spline.
    CVector end_pos;
    Pos(TotalTime(), end_pos);

    //get the distance since the end of the last laser.
    CVector line;
    line = end_pos - begin_pos;
    float distance = line.DotProduct(line);

	beam->beamEnd = end_pos;
	beamNormal = beam->beamEnd - beam->beamStart;
	beam->beamLength = beamNormal.Normalize();

    //if the last marker we placed was too far back, make one last marker.
    if (distance > 1.0f) {
        //make an entity from the previous time to the ending time of the sequence.
        beamSeg = MakeCurveSegment (begin_time, TotalTime(), begin_seg, beam);
		if (beamSeg)
		{
			beam->numSegs++;
			lastSeg->nextSeg = beamSeg;
		}

        num_entities++;
    }

    return num_entities;
}

