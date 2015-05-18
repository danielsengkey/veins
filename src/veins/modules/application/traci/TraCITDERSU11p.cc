//
// Copyright (C) 2015 Daniel Febrian Sengkey <danielsengkey.cio13@mail.ugm.ac.id>
//
// Documentation for these modules is at http://veins.car2x.org/
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// Part of my Master of Engineering thesis project.
//
// Department of Electrical Engineering and Information Technology, Gadjah Mada University
// Yogyakarta, INDONESIA
//
// Most parts of this file was derived from TraCIDemo11p.{cc,h} of Veins by Dr.-Ing. Christoph Sommer
#include "veins/modules/application/traci/TraCITDERSU11p.h"

using Veins::AnnotationManagerAccess;

Define_Module(TraCITDERSU11p);

void TraCITDERSU11p::initialize(int stage) {
    BaseWaveApplLayer::initialize(stage);
    if (stage == 0) {
        mobi = dynamic_cast<BaseMobility*> (getParentModule()->getSubmodule("mobility"));
        ASSERT(mobi);
        annotations = AnnotationManagerAccess().getIfExists();
        ASSERT(annotations);
        sentMessage = false;

        /** Show application debug message? */
        debugAppTDE = par("debugAppTDE").boolValue();

        /** Enable traffic density estimation */
        enableTDE = par("enableTDE").boolValue();
        if(enableTDE) EV << "Traffic density estimation is enabled" << endl;

        /** Vehicle classification? */
        enableClassification = par("enableClassification").boolValue();
        if(enableClassification) EV << "Vehicle classification is enabled" << endl;

        statInterval = par("statInterval").doubleValue();
        timeoutInterval = (simtime_t) par("timeoutInterval").doubleValue();
        timeoutMsgInterval = par("timeoutMsgInterval").doubleValue();
        updateRoadConditionInterval = (simtime_t) par("roadCondUpdateInterval").doubleValue();
        timeSlot = 3600/par("roadCondUpdateInterval").doubleValue();
        EV << "Time slot: " << timeSlot << endl;

        /** Traffic intervention testing. */
        trafficInterventionTestEnabled = par("interventionTest").boolValue();
        interventionTime = (simtime_t) par("interventionTime").doubleValue();

        /** Scheduling intervention for testing purpose. */
        if(trafficInterventionTestEnabled) {
            scheduledIntervention = new cMessage("scheduled intervention", INTERVENTION_TEST);
            scheduleAt(interventionTime, scheduledIntervention);
        }

        /** Assigning passenger car equivalent to related variables. */
        pceMC = par("pceMC").doubleValue();
        pceLV = par("pceLV").doubleValue();
        pceHV = par("pceHV").doubleValue();

        /** Assigning road properties, and calculate road capacity */
        basicCapacity = par("basicCapacity").doubleValue();
        correctionFactorWidth = par("CFWidth").doubleValue();
        correctionFactorSideFriction = par("CFSideFriction").doubleValue();
        correctionFactorCitySize = par("CFCitySize").doubleValue();
        numLanes = par("numberOfLanes").doubleValue();
        roadCapacity = calculateCapacity(basicCapacity, correctionFactorWidth, correctionFactorSideFriction, correctionFactorCitySize, numLanes);
        roadId = par("roadId").stringValue();

        EV << "RSU located at " << roadId << ", road capacity per-" << updateRoadConditionInterval << " seconds is " <<  roadCapacity << endl;

        /** registering all signals for the statistics output */
        vehNumberLV = registerSignal("detectedVehiclesLV");
        vehNumberHV = registerSignal("detectedVehiclesHV");
        vehNumberMC = registerSignal("detectedVehiclesMC");
        vehNumberTotal = registerSignal("detectedVehiclesTotal");
        trafficVolumeSignal = registerSignal("trafficVolume");
        VCRatioSignal = registerSignal("VCRatio");

        /** @brief Detected MC set to 0 */
        currentNumberofDetectedMC = 0;

        /** @brief Detected LV set to 0 */
        currentNumberofDetectedLV = 0;

        /** @brief Detected HV set to 0 */
        currentNumberofDetectedHV = 0;

        /** @brief Total detected vehicle set to 0 */
        currentNumberofTotalDetectedVehicles = 0;

        /** @brief Calling update statistics to record condition at t=0 */
        updateStats();

        /** @brief Calling update road condition to record condition at t=0 */
        updateRoadCondition();

        /** Scheduling the first statistics signals update */
        if(enableTDE) {
            /** Schedule self message for statistics recording */
            updateStatMsg = new cMessage("statistics update", UPDATE_STATS);
            scheduleAt(simTime() + statInterval, updateStatMsg);

            /** Schedule self message for road condition update. */
            RoadConditionUpdateMsg = new cMessage("Update road condition information", CHECK_ROAD);
            scheduleAt(simTime() + updateRoadConditionInterval, RoadConditionUpdateMsg);

            if(timeoutInterval>0){
                timeoutMsg = new cMessage("check for timed out node", CHECK_TIMEOUT);
                scheduleAt(simTime() + timeoutMsgInterval, timeoutMsg);
            }
        }

        /** @brief initializing vehicles array with -1 to prevent conflict with node id within simulation.
         * Store self id in the first index.
         */
        for (int i=0; i < maxVehicles; i++)
        {
            listedVehicles[i].id = -1;
            listedVehicles[i].vType = "undefined";
        }
        if (debugAppTDE==true) showInfo(currentNumberofTotalDetectedVehicles);
    }
}

void TraCITDERSU11p::onBeacon(WaveShortMessage* wsm) {
    EV << "### REPORT OF RSU WITH ID " << myId << " ###" << endl;
    EV << "Received BEACON from " << wsm->getSenderAddress() << " on " << wsm->getArrivalTime() << " type " << wsm->getWsmData() << "."<< endl;

    if((enableTDE) && ((strcmp(wsm->getWsmData(),"MC")==0)||(strcmp(wsm->getWsmData(),"LV")==0)||(strcmp(wsm->getWsmData(),"HV")==0)))
    {
        if (indexedCar(wsm->getSenderAddress(), currentNumberofTotalDetectedVehicles)==false) {
            append2List(wsm->getSenderAddress(), currentNumberofTotalDetectedVehicles,  wsm->getArrivalTime(), wsm->getWsmData());
        } else updateLastSeenTime(wsm->getSenderAddress(), currentNumberofTotalDetectedVehicles, wsm->getArrivalTime());
        if (debugAppTDE==true) showInfo(currentNumberofTotalDetectedVehicles);
    }
    EV << "########## END OF REPORT ##########" << endl;
}

void TraCITDERSU11p::onData(WaveShortMessage* wsm) {
    findHost()->getDisplayString().updateWith("r=16,green");

    annotations->scheduleErase(1, annotations->drawLine(wsm->getSenderPos(), mobi->getCurrentPosition(), "blue"));

    EV << "Received DATA from " << wsm->getSenderAddress() << " on " << wsm->getArrivalTime() << endl;

    if (!sentMessage) sendMessage(wsm->getWsmData());
}

void TraCITDERSU11p::sendMessage(std::string blockedRoadId) {
    sentMessage = true;
    t_channel channel = dataOnSch ? type_SCH : type_CCH;
    WaveShortMessage* wsm = prepareWSM("data", dataLengthBits, channel, dataPriority, -1,2);
    wsm->setWsmData(blockedRoadId.c_str());
    sendWSM(wsm);
}
void TraCITDERSU11p::sendWSM(WaveShortMessage* wsm) {
    sendDelayedDown(wsm,individualOffset);
}

/* Following lines of methods are those I built, (some are overloaded) in regards of my thesis */
bool TraCITDERSU11p::indexedCar(short carId, short counter) {
    int i;
    bool indexedStatus = false;
    for(i=0; i < counter; i++) {
        if(listedVehicles[i].id == carId) {
            indexedStatus = true;

            break;
        } //endif
    } //endfor
    if (indexedStatus) EV << "Vehicle with ID = " << carId << " is already indexed." << endl;
    else EV << "Vehicle with ID = " << carId << " is still unindexed." << endl;
    return indexedStatus;
} //end method

void TraCITDERSU11p::append2List(short carId, short firstEmptyArrayIndex, simtime_t messageTime, std::string vType) {
    listedVehicles[firstEmptyArrayIndex].id = carId;
    listedVehicles[firstEmptyArrayIndex].lastSeenAt = messageTime;
    listedVehicles[firstEmptyArrayIndex].vType = vType;
    EV << "Appending car with id " << carId <<" type "<< vType << " to the list of known vehicle." <<endl;

    /* @brief Increase related counting variable
     * The total number always increased for each vehicle
     */
    if (vType == "MC") { currentNumberofDetectedMC++;
    } else if (vType == "LV") { currentNumberofDetectedLV++;
    } else if (vType == "HV") { currentNumberofDetectedHV++;
    } else DBG << "Unknown vehicle type" << vType << endl;
    currentNumberofTotalDetectedVehicles++;
}

void TraCITDERSU11p::showInfo(short counter){
    EV << "Listed vehicles are:"<<endl;
    for(int i=0; i < counter; i++) EV << "[" << i << "] " << listedVehicles[i].id << "\ttype\t" << listedVehicles[i].vType << "\tat\t" << listedVehicles[i].lastSeenAt << endl;
    EV << endl;

    EV << "Number of detected light vehicles\t: " << currentNumberofDetectedLV << endl;
    EV << "Number of detected heavy vehicles\t: " << currentNumberofDetectedHV << endl;
    EV << "Number of detected motorcycles\t: " << currentNumberofDetectedMC << endl;
    EV << "Total number of detected vehicle\t: " << currentNumberofTotalDetectedVehicles << endl;
}

void TraCITDERSU11p::updateLastSeenTime(short carId, short counter, simtime_t messageTime){
    int i;
    for (i=0; i< counter; i++) {
        if (listedVehicles[i].id == carId) {
            EV << "Updating record for vehicle with ID " << carId << " with previously last seen at " << listedVehicles[i].lastSeenAt << ", now lastSeenAt = " << messageTime <<"."<<endl;
            listedVehicles[i].lastSeenAt = messageTime;
            break;
        }
    }
}

void TraCITDERSU11p::updateStats() {
    if(debugAppTDE==true) EV << "Updating statistics record" << endl;
    emit(vehNumberMC, currentNumberofDetectedMC);
    emit(vehNumberLV, currentNumberofDetectedLV);
    emit(vehNumberHV, currentNumberofDetectedHV);
    emit(vehNumberTotal, currentNumberofTotalDetectedVehicles);
}

/** delete timed out vehicle from the list and restructure the array */
void TraCITDERSU11p::deleteAndRestructureArray() {
    int i,j;

    for (i=0; i<currentNumberofTotalDetectedVehicles; i++) {
        if(debugAppTDE) EV << "Checking last seen time for list index [" << i << "], ID: " << listedVehicles[i].id << endl;

        if (listedVehicles[i].lastSeenAt < (simTime()-timeoutInterval)) {
            EV << "Found timed out node in index. ID: " << listedVehicles[i].id
                    << ", type " << listedVehicles[i].vType
                    << " was last seen at " << listedVehicles[i].lastSeenAt
                    << " while current time is " << simTime() << "." << endl;

            if (listedVehicles[i].vType == "MC") {
                currentNumberofDetectedMC--;
            } else if (listedVehicles[i].vType == "LV") {
                currentNumberofDetectedLV--;
            } else if (listedVehicles[i].vType == "HV") {
                currentNumberofDetectedHV--;
            } else EV << "Node ID: " << myId << "Unknown vehicle type from node: " << listedVehicles[i].id << endl;

            for (j=i; j<currentNumberofTotalDetectedVehicles; j++) {
                listedVehicles[j] = listedVehicles[j+1];
            }
            currentNumberofTotalDetectedVehicles--;
        }
    }

    if(debugAppTDE) showInfo(currentNumberofTotalDetectedVehicles);
}

double TraCITDERSU11p::calculateCapacity(double bc, double cfw, double csf, double cfcs, double numLanes) {
    double capacity;

    //Calculation based on formula in MKJI.
    //Also available in Tamin's book, pp. 107.
    DBG << "App: basicCapacity " << basicCapacity << endl;
    DBG << "App: numLanes " << numLanes << endl;
    DBG << "App: correctionFactorWidth " << correctionFactorWidth << endl;
    DBG << "App: correctionFactorSideFriction " << correctionFactorSideFriction << endl;
    DBG << "App: correctionFactorCitySize " << correctionFactorCitySize << endl;
    capacity = (basicCapacity*numLanes*correctionFactorWidth*correctionFactorSideFriction*correctionFactorCitySize)/timeSlot;

    return capacity;
}

double TraCITDERSU11p::trafficVolume() {
    double volume;

    /**
     * If vehicle classification is enabled, then calculate traffic volume by multiplying a vehicle type with its passenger car equivalent.
     * If vehicle classification is disabled, then traffic volume is equal to current number of detected vehicle since all type will be multiplied with 1.0
     */
    if(enableClassification) volume = (currentNumberofDetectedMC*pceMC) + (currentNumberofDetectedLV*pceLV) + (currentNumberofDetectedHV*pceHV);
    else volume = currentNumberofTotalDetectedVehicles;
    return volume;
}

double TraCITDERSU11p::VCRatio(double volume) {
    double vcr;

    //Calculate VC ratio
    vcr = volume/roadCapacity;

    return vcr;
}

void TraCITDERSU11p::updateRoadCondition() {
    double currentVolume;
    double vcr;

    currentVolume = trafficVolume();
    vcr = VCRatio(currentVolume);
    EV << "At [" << simTime() << "], Current volume is " << currentVolume << " and VC Ratio is " << vcr << endl;
    emit(trafficVolumeSignal, currentVolume);
    emit(VCRatioSignal, vcr);

    if(vcr>0.84) {
        EV << "VC Ratio threshold exceeded. Broadcasting message." << endl;
        sentMessage = false;
        sendMessage(roadId);
    }
}

void TraCITDERSU11p::handleSelfMsg(cMessage* msg) {
    switch (msg->getKind()) {
    case UPDATE_STATS: {
        updateStats();
        scheduleAt(simTime() + statInterval, updateStatMsg);
        break;
    }
    case CHECK_TIMEOUT: {
        if(enableTDE) deleteAndRestructureArray();
        scheduleAt(simTime() + timeoutMsgInterval, timeoutMsg);
        break;
    }
    case CHECK_ROAD: {
        updateRoadCondition();
        scheduleAt(simTime() + updateRoadConditionInterval, RoadConditionUpdateMsg);
        break;
    }
    case INTERVENTION_TEST: {
        EV << "Testing traffic intervention!" << endl;
        sendMessage(roadId);
        break;
    }
    default: {
        if (msg)
            DBG << "APP: Error: Got Self Message of unknown kind! Name: " << msg->getName() << endl;
        break;
    }
    }
}

void TraCITDERSU11p::finish() {
    if (sendBeaconEvt->isScheduled()) {
        cancelAndDelete(sendBeaconEvt);
    } else {
        delete sendBeaconEvt;
    }

    if (timeoutMsg->isScheduled()) {
        cancelAndDelete(timeoutMsg);
    } else {
        delete timeoutMsg;
    }

    if (updateStatMsg->isScheduled()) {
        cancelAndDelete(updateStatMsg);
    } else {
        delete updateStatMsg;
    }

    if(RoadConditionUpdateMsg->isScheduled()) {
        cancelAndDelete(RoadConditionUpdateMsg);
    } else {
        delete RoadConditionUpdateMsg;
    }

    if(trafficInterventionTestEnabled){
        if(scheduledIntervention->isScheduled()) {
            cancelAndDelete(scheduledIntervention);
        } else {
            delete scheduledIntervention;
        }
    }
    findHost()->unsubscribe(mobilityStateChangedSignal, this);

}
