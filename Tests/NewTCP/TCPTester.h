//
// Copyright (C) 2004 Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
//

#ifndef __TCPTESTER_H
#define __TCPTESTER_H

#include <omnetpp.h>
#include "IPAddress.h"
#include "IPDatagram_m.h"
#include "TCPSegment_m.h"
#include "TCPDump.h"


/**
 * Base class for TCP testing modules.
 */
class TCPTesterBase : public cSimpleModule
{
  protected:
    int fromASeq;
    int fromBSeq;

    TCPDumper tcpdump;

  protected:
    void dump(TCPSegment *seg, bool fromA, const char *comment=NULL);

  public:
    TCPTesterBase(const char *name, cModule *parent);
    virtual void initialize();
    virtual void finish();
};


/**
 * Dumps every packet using the TCPDumper class, and in addition it can delete,
 * delay or duplicate TCP segments, and insert new segments.
 *
 * Script format:
 *
 * <i><segment><operation><args>; <segment><operation><args>; ...</i>
 *
 * e.g.:
 *
 * <tt>A2 delete; B3 delete; A3 delay 0.2; A4 copy 1.0,1.2; ...</tt>
 *
 * Where:
 * - <i><segment></i>: A10 means 10th segment arriving from A
 * - <i><operation></i> can be <tt>delete</tt>, <tt>delay</tt> or <tt>copy</tt>:
 *    - <tt>delete</tt>: removes (doesn't copy) segment
 *    - <tt>delay</tt> <i><delay></i>: forwards segment after a delay
 *    - <tt>copy</tt> <i><delay1>,<delay2>,<delay3>,...</i>:
 *      forwards a copy of the segment after the given delays
 *      (<tt>copy 0.5</tt> is the same as <tt>delay 0.5</tt>)
 */
class TCPScriptableTester : public TCPTesterBase
{
  protected:
    enum {CMD_DELETE,CMD_COPY}; // "delay" is same as "copy"
    typedef std::vector<simtime_t> DelayVector;
    struct Command
    {
        bool fromA;  // direction
        int segno;   // segment number
        int command; // CMD_DELETE, CMD_COPY
        DelayVector delays;  // arg list
    };
    typedef std::vector<Command> CommandVector;
    CommandVector commands;

  protected:
    void parseScript(const char *script);
    void dispatchSegment(TCPSegment *seg);
    void processIncomingSegment(TCPSegment *seg, bool fromA);

  public:
    TCPScriptableTester(const char *name, cModule *parent);
    virtual void initialize();
    virtual void handleMessage(cMessage *msg);
};


/**
 * Randomly delete, delay etc packets.
 */
class TCPRandomTester : public TCPTesterBase
{
  protected:
    double pdelete;
    double pdelay;
    double pcopy;
    cPar *numCopies;
    cPar *delay;

  protected:
    void dispatchSegment(TCPSegment *seg);
    void processIncomingSegment(TCPSegment *seg, bool fromA);

  public:
    TCPRandomTester(const char *name, cModule *parent);
    virtual void initialize();
    virtual void handleMessage(cMessage *msg);
};

#endif


