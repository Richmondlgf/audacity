//////////////////////////////////////////////////////////////////////
//  iAVC -- integer Automatic Volume Control (on samples given it)
//
//	Copyright (C) 2002 Vincent A. Busam
//				  15754 Adams Ridge
//		  		  Los Gatos, CA 95033
//		  email:  vince@busam.com
//
//	This library is free software; you can redistribute it and/or
//	modify it under the terms of the GNU Lesser General Public
//	License as published by the Free Software Foundation; either
//	version 2.1 of the License, or (at your option) any later version.
//
//	This library is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//	Lesser General Public License for more details.
//
//	You should have received a copy of the GNU Lesser General Public
//	License along with this library; if not, write to the Free Software
//	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA


//  This code implements a "poor man's" dynamic range compression algorithm
//	that was build on hueristics.  It's purpose is to perform dynamic range
//  compression in real time using only integer arithmetic.  Processing time
//  is more important than memory.  It acts like an automatic volume control,
//  frequently adjusting the volume (gain) based on an average of the current
//  sound samples

#if  !defined(IAVC_INLINE) || ( !defined(IAVC_SETNEXTSAMPLE) && !defined(IAVC_GETNEXTSAMPLE) && !defined(IAVC_ADJUSTMULTIPLIER) )

#ifdef _WINDOWS
	//#include "stdafx.h"		// don't use precompiled headers on this file
#endif

#ifndef __cplusplus
	// You really should consider using C++.  
	// Granted it is not needed or even useful in all situations, but real
	//		object oriented design and code (not faux OO code like in MFC)
	//		has lots of benefits.
#endif

#include "iAVC.h"

#if (defined ( _WINDOWS ) | defined ( _DEBUG ))
    #define c_WithDebug 1       // should be = 1
#else
    #define c_WithDebug 0
#endif

///////////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
//
//      iAVC constructor
//
///////////////////////////////////////////////////////////////////////////////

AutoVolCtrl::AutoVolCtrl()
{
    m_pSampleList               = NULL;
    m_nSampleWindowSize         = DEFAULT_SAMPLE_WINDOW_SIZE;
    m_nSamplesInAvg             = DEFAULT_ADJUSTER_WINDOW_SIZE;
    m_nLookAheadWindowSize      = DEFAULT_LOOKAHEAD_WINDOW_SIZE;
    m_nMinSamplesBeforeSwitch   = DEFAULT_MINIMUM_SAMPLES_BEFORE_SWITCH;
    m_nNumTracks                = DEFAULT_NUMBER_OF_TRACKS;
    m_nMaxChangePct             = DEFAULT_MAX_PCT_CHANGE_AT_ONCE;

	SetSampleWindowSize ( m_nSampleWindowSize, 
                          m_nSamplesInAvg, 
                          m_nLookAheadWindowSize );
	Reset();

	// set multipliers to a nil transform
	for ( int i = 0 ; i < MULTIPLY_PCT_ARRAY_SIZE ; ++i )
		m_nMultiplyPct [ i ] = APPLY_MULTIPLY_FACTOR ( 1 );		// default to no transform	
}

///////////////////////////////////////////////////////////////////////////////
//
//      iAVC destructor
//
///////////////////////////////////////////////////////////////////////////////

AutoVolCtrl::~AutoVolCtrl()
{
	// Dump diagnostic information
	log1(CN_iAVC,LL_DEBUG,0, "Sample Window Size    %d\n", m_nSampleWindowSize );
	log1(CN_iAVC,LL_DEBUG,0, "Adjuster Window Size  %d\n", m_nSamplesInAvg );
	log1(CN_iAVC,LL_DEBUG,0, "Min Samples to Switch %d\n", m_nMinSamplesBeforeSwitch );
	log1(CN_iAVC,LL_DEBUG,0, "Pct Change threshold  %d\n", m_nMaxChangePct );
	log1(CN_iAVC,LL_DEBUG,0, "Number of Samples   = %d\n", m_nTotalSamples );
	log1(CN_iAVC,LL_DEBUG,0, "Multiplier changes  = %d\n", m_nNumMultiplerChanges ); 
	log1(CN_iAVC,LL_DEBUG,0, "Number of clips     = %d\n", m_nClips );

	if ( m_pSampleList != NULL )
		delete []m_pSampleList;
}

///////////////////////////////////////////////////////////////////////////////
//
//      Reset
//
///////////////////////////////////////////////////////////////////////////////

void AutoVolCtrl::Reset()
{
    ZeroSampleWindow();
	SetMinSamplesBeforeSwitch ( m_nMinSamplesBeforeSwitch );		
	SetMaxPctChangeAtOnce ( m_nMaxChangePct );			// e.g. 10% 
	SetNumberTracks ( m_nNumTracks );
	
	// set our internal data
	m_nSampleSum = 0;				
	m_nSamplesInSum = 0;
	m_nCurrentMultiplier = APPLY_MULTIPLY_FACTOR ( 1 );
	m_nTotalSamples = 0;
	m_nNumMultiplerChanges = 0;
	m_nClips = 0;
    m_nLookaheadSum = 0;
    m_nSamplesInLookahead = 0;
    m_nNumSamplesBeforeNextSwitch = 0;  // allows switch on first GetSample
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetSampleWindowSize
//
///////////////////////////////////////////////////////////////////////////////

bool AutoVolCtrl::SetSampleWindowSize ( unsigned long nSampleWindowSize, 
										 unsigned long nAdjusterWindowSize,
                                         unsigned long nLookAheadWindowSize )
{
	if ( nSampleWindowSize > MAX_SAMPLE_WINDOW_SIZE )
		return false;		// sums may overflow and we use int for indicies

	if ( nSampleWindowSize < nAdjusterWindowSize + nLookAheadWindowSize )
		return false;

	m_nSamplesInAvg = nAdjusterWindowSize;
    m_nLookAheadWindowSize = nLookAheadWindowSize;

    if ( m_nSampleWindowSize != nSampleWindowSize || m_pSampleList == NULL )
    {   // window size has changed
	    m_nSampleWindowSize = nSampleWindowSize;
	    if ( m_pSampleList )
		    delete m_pSampleList;
	    m_pSampleList = new Sample [ m_nSampleWindowSize ];
    }

	// initialize a circular list of samples 
	for ( unsigned long j = 0 ; j < m_nSampleWindowSize ; ++j )
	{
		m_pSampleList [ j ].m_pNext = &(m_pSampleList[j + 1]);
		m_pSampleList [ j ].m_nLeft = 0;
		m_pSampleList [ j ].m_nRight = 0;
		m_pSampleList [ j ].m_nSampleValid = 0;         // false
        m_pSampleList [ j ].m_nSampleAbsSum = 0;
        // set average partner
		m_pSampleList [ j ].m_pAvgPartner = ( j < m_nSamplesInAvg ) ? 
												&(m_pSampleList [ m_nSampleWindowSize - m_nSamplesInAvg + j]) :
												&(m_pSampleList [ j - m_nSamplesInAvg ]) ;
        // set lookahead partner
        m_pSampleList [ j ].m_pLookaheadPartner = ( j < m_nLookAheadWindowSize ) ?
												&(m_pSampleList [ m_nSampleWindowSize - m_nLookAheadWindowSize + j]) :
												&(m_pSampleList [ j - m_nLookAheadWindowSize ]) ;
	}
	m_pSampleList [ m_nSampleWindowSize - 1 ].m_pNext = &(m_pSampleList[0]);  // last points to first

    ZeroSampleWindow();

    if ( c_WithDebug )
    {
	    //for ( j = 0 ; j < m_nSampleWindowSize ; ++j )
	    //{
        //    unsigned long nNext = ( m_pSampleList [ j ].m_pNext - m_pSampleList );
        //    unsigned long nAvgp = ( m_pSampleList [ j ].m_pAvgPartner - m_pSampleList );
        //    unsigned long nLkap = ( m_pSampleList [ j ].m_pLookaheadPartner - m_pSampleList );
        //    log4(CN_iAVC,LL_DEBUG,0, "this=%d, next=%d, AvgPartner=%d, LookAheadPartner = %d",
        //                                        j, nNext, nAvgp, nLkap );
	    //}
    }

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetMinSamplesBeforeSwitch
//
///////////////////////////////////////////////////////////////////////////////

bool AutoVolCtrl::SetMinSamplesBeforeSwitch ( unsigned long nMinSamplesBeforeSwitch )
{
	if ( m_nSampleWindowSize < nMinSamplesBeforeSwitch || 
         nMinSamplesBeforeSwitch < MIN_MINIMUM_SAMPLES_BEFORE_SWITCH )
		return false;

	m_nMinSamplesBeforeSwitch     = nMinSamplesBeforeSwitch;
    m_nNumSamplesBeforeNextSwitch = 0;

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetMaxPctChangeAtOnce
//
///////////////////////////////////////////////////////////////////////////////

void AutoVolCtrl::SetMaxPctChangeAtOnce ( unsigned long nPctChange )
{
    m_nMaxChangePct = nPctChange;
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetMultipliers
//
///////////////////////////////////////////////////////////////////////////////

void AutoVolCtrl::SetMultipliers ( unsigned short int nValueWanted [ MULTIPLY_PCT_ARRAY_SIZE ] )
{
	for ( int i = 1 ; i < MULTIPLY_PCT_ARRAY_SIZE ; ++i )
	{
		m_nMultiplyPct [ i ] = APPLY_MULTIPLY_FACTOR ( nValueWanted [ i ] ) / long ( i );
		if ( ( i % 1000 ) == 0 )
		    log3(CN_iAVC,LL_DEBUG,0, "SetMultipliers at sample %d, =%d (0x%X)\n",
										i,
										m_nMultiplyPct [ i ],
										m_nMultiplyPct [ i ] );
	}
	m_nMultiplyPct [ 0 ] = m_nMultiplyPct [ 1 ];
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetNumberTracks
//
///////////////////////////////////////////////////////////////////////////////

bool AutoVolCtrl::SetNumberTracks ( unsigned int nNumTracks )
{
    if ( nNumTracks > MAX_NUMBER_OF_TRACKS )
        return false;
	m_nNumTracks = nNumTracks;
    return true;
}

///////////////////////////////////////////////////////////////////////////////
//
//      ZeroSampleWindow
//
///////////////////////////////////////////////////////////////////////////////

void AutoVolCtrl::ZeroSampleWindow()
{
	// initialize a circular list of samples 
	for ( unsigned long j = 0 ; j < m_nSampleWindowSize ; ++j )
	{
		m_pSampleList [ j ].m_nLeft = 0;
		m_pSampleList [ j ].m_nRight = 0;
		m_pSampleList [ j ].m_nSampleValid = 0;         // false
        m_pSampleList [ j ].m_nSampleAbsSum = 0;
    }

	// set subscripts for where next data goes or comes from
	m_pNextSet = &m_pSampleList [ 0 ];
	m_pNextGet = &m_pSampleList [ 0 ];
}

///////////////////////////////////////////////////////////////////////////////
//
//      SetNextSample
//
///////////////////////////////////////////////////////////////////////////////

bool AutoVolCtrl::SetNextSample ( short int left, short int right )
{

#endif      // !defined...
#if  defined(IAVC_SETNEXTSAMPLE) 

	// take out of our sum the sample m_nSamplesInAvg before the sample just before the lookahead window
    Sample* pAddToSum      = m_pNextSet->m_pLookaheadPartner;   // node just before lookahead window
	Sample* pRemoveFromSum = pAddToSum->m_pAvgPartner;          // node to remove from sample sum

    //if ( m_nTotalSamples <= 2200 )
    //{   // TEMP
	//	log8(CN_iAVC,LL_DEBUG,0, 
    //                    "# = %d, sum = %d,"
    //                    ", nextSet=%d, AddTo=%d (%d), RemoveFrom=%d (%d), newAbs=%d",
	//					m_nSamplesInSum, 
    //                    m_nSampleSum, 
    //                    m_pNextSet - m_pSampleList,
    //                    pAddToSum - m_pSampleList, pAddToSum->m_nSampleAbsSum,
    //                    pRemoveFromSum - m_pSampleList, pRemoveFromSum->m_nSampleAbsSum,
    //                    abs ( left ) + abs ( right ) );
    //}

	// take this sample out of the sample sum (if valid)
	m_nSampleSum -= pRemoveFromSum->m_nSampleAbsSum;
	m_nSamplesInSum -= pRemoveFromSum->m_nSampleValid;

    // form total value for this cell
    m_pNextSet->m_nSampleAbsSum = abs ( left ) + abs ( right );
	// put in new sample
	m_pNextSet->m_nLeft = left;	
	m_pNextSet->m_nRight = right;
	m_pNextSet->m_nSampleValid = 1;     // true, node will now always have a valid sample in it

    // add a node's samples into the sample sum (if valid)
	m_nSampleSum += pAddToSum->m_nSampleAbsSum;
	m_nSamplesInSum += pAddToSum->m_nSampleValid;


    //NOTUSED - not using lookahead
    //if ( m_nLookAheadWindowSize > 0 )
    //{   // Figure out lookahead average for our lookahead partner
    //    Sample* pLookaheadPartner = pAddToSum;      // take this nodes samples out of lookahead sum
    //	// take this sample out of the sum (if valid)
	//	m_nLookaheadSum -= pLookaheadPartner->m_nSampleAbsSum;
	//	m_nSamplesInLookahead -= pLookaheadPartner->m_nSampleValid;
    //
	//    // add into the lookahead sum the new values
	//    ++m_nSamplesInLookahead;
	//    m_nLookaheadSum += m_pNextSet->m_nSampleAbsSum;
    //}

	m_pNextSet = m_pNextSet->m_pNext;

#endif      // defined(IAVC_SETNEXTSAMPLE) 
#if  !defined(IAVC_INLINE) || ( !defined(IAVC_SETNEXTSAMPLE) && !defined(IAVC_GETNEXTSAMPLE) && !defined(IAVC_ADJUSTMULTIPLIER) )

    return true;
}

///////////////////////////////////////////////////////////////////////////////
//
//      GetNextSample
//
///////////////////////////////////////////////////////////////////////////////

bool AutoVolCtrl::GetNextSample ( short int & left, short int & right )
{

#endif      // !defined...
#if  defined(IAVC_GETNEXTSAMPLE) 

	// Note: If Puts circle around before we get the samples, then we'll lose one
	//					whole round of samples.

    int  nClip;     // not used unless c_WithDebug is true

#if  defined(IAVC_INLINE)
#undef IAVC_GETNEXTSAMPLE
#define IAVC_ADJUSTMULTIPLIER
//#pragma message("inlining AdjustMultiplier 1st time")
#include "DynRangeComp.cpp"
#define IAVC_GETNEXTSAMPLE
#undef  IAVC_ADJUSTMULTIPLIER
#else
    if ( m_pNextGet == m_pNextSet )
	{	
		return false;				// no sample to give
	}

	AdjustMultiplier();
#endif
   
    if ( c_WithDebug )
    {
	    ++m_nTotalSamples;

	    if ( ( m_nTotalSamples % 10000 ) <= 1 )
	    {
		    log4(CN_iAVC,LL_DEBUG,0, 
                            "Number of samples in sum = %d, sample sum = %d, tracks = %d, sample avg = %d\n",
						    m_nSamplesInSum, m_nSampleSum, m_nNumTracks,
						    ( m_nSampleSum / m_nSamplesInSum ) / m_nNumTracks );
	    }

	    nClip = 0;
    }

	long lLeft = UNDO_MULTIPLY_FACTOR ( m_nCurrentMultiplier * m_pNextGet->m_nLeft  );
	left = (short int) ( lLeft );

	long lRight = UNDO_MULTIPLY_FACTOR ( m_nCurrentMultiplier * m_pNextGet->m_nRight );
	right = (short int) ( lRight );

    if ( long ( left ) != lLeft || long ( right ) != lRight )
    {   // We had a clip, see if we can adjust multiplier down.

        // What do we do?  If this is a momentary pop, like a pop on a record, we should
        //      do nothing.  But most audio today is probably from CDs and therefore
        //      probably clean.  So let's be bold and ASSUME that we're just moving into
        //      a loud section from a softer section (which can be why we have a high
        //      multiplier for this sample).  In this case, let's just change the multiplier
        //      now and not wait for the end of the next change window.  To figure out the
        //      new multiplier, we'll just use this sample.

        m_nCurrentMultiplier = m_nMultiplyPct [ m_pNextGet->m_nSampleAbsSum / m_nNumTracks ];  // always positive

//        // This path will take extra time, but shouldn't occur very often.
//        m_nNumSamplesBeforeNextSwitch = 0;      // for multiplier adjustment
//        ++m_nNumSamplesBeforeNextSwitch;        // don't do this twice for a sample, already invoked AdjustMultiplier
//
//#if  defined(IAVC_INLINE)
//#undef IAVC_GETNEXTSAMPLE
//#define IAVC_ADJUSTMULTIPLIER
////#pragma message("inlining AdjustMultiplier 2nd time")
//#include "DynRangeComp.cpp"
//#define IAVC_GETNEXTSAMPLE
//#undef  IAVC_ADJUSTMULTIPLIER
//#else
//	    AdjustMultiplier();
//#endif

	    long lLeft = UNDO_MULTIPLY_FACTOR ( m_nCurrentMultiplier * m_pNextGet->m_nLeft  );
	    left = (short int) ( lLeft );

	    long lRight = UNDO_MULTIPLY_FACTOR ( m_nCurrentMultiplier * m_pNextGet->m_nRight );
	    right = (short int) ( lRight );

	    if ( long ( left ) != lLeft )
	    {
		    left = m_pNextGet->m_nLeft;	// don't clip, use original values instead
            if ( c_WithDebug )
            {
		        nClip = 1;
            }
	    }

	    if ( long ( right ) != lRight )
	    {
		    right = m_pNextGet->m_nRight;	// don't clip, use original values instead
            if ( c_WithDebug )
		        nClip = 1;
	    }
    }

    if ( c_WithDebug )
    {
	    if ( nClip != 0 )
	    {
		    m_nClips += nClip;
		    if ( ( m_nClips % 250 ) == 0 )
		    {	// m_nTotalSamples may be off if buffered (i.e. more put samples than get samples done)
			    log4(CN_iAVC,LL_DEBUG,0, "Sample %d clipped, orig left=%d, right=%d, total clips=%d\n", 
											    m_nTotalSamples, m_pNextGet->m_nLeft, m_pNextGet->m_nRight, m_nClips ); 
			    log4(CN_iAVC,LL_DEBUG,0, "Left %d -> %d, Right %d -> %d\n", lLeft, left, lRight, right );
		    }
	    }

        if ( ( m_nTotalSamples % 5000 ) == 0 )
	    {
		    log4(CN_iAVC,LL_DEBUG,0, "Transformed %d->%d  %d->%d\n", m_pNextGet->m_nLeft, left, m_pNextGet->m_nRight, right );
	    }
    }

	m_pNextGet = m_pNextGet->m_pNext;

#endif      // defined(IAVC_GETNEXTSAMPLE) 
#if  !defined(IAVC_INLINE) || ( !defined(IAVC_SETNEXTSAMPLE) && !defined(IAVC_GETNEXTSAMPLE) && !defined(IAVC_ADJUSTMULTIPLIER) )

	return true;
}

///////////////////////////////////////////////////////////////////////////////
//
//      AdjustMultiplier
//
///////////////////////////////////////////////////////////////////////////////

void AutoVolCtrl::AdjustMultiplier()
{
    // TEMPORARY DEBUG CODE
    if ( c_WithDebug && m_nTotalSamples >= 466930L && m_nTotalSamples <= 466973L )
    {
		log3(CN_iAVC,LL_DEBUG,0, "DEBUG   at sample %d, mul now=0x%X (%d)\n", 
									m_nTotalSamples,
									m_nCurrentMultiplier, 
									m_nCurrentMultiplier );
        long nLookaheadAvg;
        if ( m_nSamplesInLookahead > 0 )
            nLookaheadAvg= ( m_nLookaheadSum/m_nSamplesInLookahead ) / m_nNumTracks;
        else
            nLookaheadAvg = 0;
		log4(CN_iAVC,LL_DEBUG,0, "        sample max=%d, sample win avg=%d, lookahead avg=%d, avg multiplier=0x%X\n",
                                    max(abs(m_pNextGet->m_nLeft),abs(m_pNextGet->m_nRight)),
                                    ( m_nSampleSum / m_nSamplesInSum ) / m_nNumTracks,
                                    nLookaheadAvg,
                                    m_nMultiplyPct [ nLookaheadAvg ] );
    }


#endif  //      !defined...
#if  defined(IAVC_ADJUSTMULTIPLIER) 
//#pragma message("inlining AdjustMultiplier")

    --m_nNumSamplesBeforeNextSwitch;
	if ( m_nNumSamplesBeforeNextSwitch <= 0 )
	{	// long time since last change, see if it is time to change the multiplier
        long nCurSampleAvg = ( m_nSamplesInSum <= 0 ) ? 0 :
                                            ( m_nSampleSum / m_nSamplesInSum ) / m_nNumTracks;
		long nNewMultiplier = m_nMultiplyPct [ nCurSampleAvg ];         // always positive
		long nMultiplierDiff = nNewMultiplier - m_nCurrentMultiplier;   // positive or negative
		// if new multiplier is 1, force change to get to 1  (nChangeThreshold always positive)
		long nChangeThreshold = ( nMultiplierDiff != 0 &&
								  nNewMultiplier == APPLY_MULTIPLY_FACTOR ( 1 ) ) ?
										nMultiplierDiff :       
										m_nCurrentMultiplier * m_nMaxChangePct / 100; // % of current multiplier
        //NOTUSED - not using lookahead
        //unsigned long nLookaheadAvg;
        //if ( m_nSamplesInLookahead > 0 )
        //    nLookaheadAvg = ( m_nLookaheadSum/m_nSamplesInLookahead ) / m_nNumTracks;
        //else
        //    nLookaheadAvg = 0;
        //long nLookaheadMultiplier = m_nMultiplyPct [ nLookaheadAvg ];

		if ( nMultiplierDiff >= nChangeThreshold )
		{	// adjust multiplier up
			m_nCurrentMultiplier = nNewMultiplier;  // or  m_nCurrentMultiplier += nChangeThreshold;
            m_nNumSamplesBeforeNextSwitch = m_nMinSamplesBeforeSwitch;
            if ( c_WithDebug )
            {
			    ++m_nNumMultiplerChanges;
		        log5(CN_iAVC,LL_DEBUG,0, "Multiplier up   at sample %d, current avg=%d, now=0x%X (%d), want=0x%X\n", 
										    m_nTotalSamples,
                                            nCurSampleAvg,
										    m_nCurrentMultiplier, 
										    m_nCurrentMultiplier, 
										    nNewMultiplier);
                //NOTUSED - not using lookahead
		        //log2(CN_iAVC,LL_DEBUG,0, "                lookahead: avg=%d, avg multiplier=0x%X\n",
                //                            nLookaheadAvg,
                //                            nLookaheadMultiplier );
            }
		}
		else if ( nMultiplierDiff <= - nChangeThreshold )
		{	// adjust multiplier down
			m_nCurrentMultiplier = nNewMultiplier;  // or m_nCurrentMultiplier -= nChangeThreshold;
            m_nNumSamplesBeforeNextSwitch = m_nMinSamplesBeforeSwitch;
            if ( c_WithDebug )
            {
			    ++m_nNumMultiplerChanges;
		        log5(CN_iAVC,LL_DEBUG,0, "Multiplier down at sample %d, current avg=%d, now=0x%X (%d), want=0x%X\n", 
										    m_nTotalSamples,
                                            nCurSampleAvg,
										    m_nCurrentMultiplier, 
										    m_nCurrentMultiplier, 
										    nNewMultiplier);
                //NOTUSED - not using lookahead
		        //log2(CN_iAVC,LL_DEBUG,0, "                lookahead: avg=%d, avg multiplier=0x%X\n",
                //                            nLookaheadAvg,
                //                            nLookaheadMultiplier );
            }
		}
	}

#endif      // defined(IAVC_ADJUSTMULTIPLIER) 
#if  !defined(IAVC_INLINE) || ( !defined(IAVC_SETNEXTSAMPLE) && !defined(IAVC_GETNEXTSAMPLE) && !defined(IAVC_ADJUSTMULTIPLIER) )

    return;
}

#endif      // !defined...

