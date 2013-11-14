/*
    ------------------------------------------------------------------

    This file is part of the Open Ephys GUI
    Copyright (C) 2013 Open Ephys

    ------------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include "SpikeDetector.h"
#include "SpikeSortBoxes.h"
#include "Visualization/SpikeDetectCanvas.h"
#include "Channel.h"
class spikeSorter;

SpikeDetector::SpikeDetector()
    : GenericProcessor("Spike Detector"),
      overflowBuffer(2,100), dataBuffer(overflowBuffer),
      overflowBufferSize(100), currentElectrode(-1)
{
    //// the standard form:
    electrodeTypes.add("single electrode");
    electrodeTypes.add("stereotrode");
    electrodeTypes.add("tetrode");
	uniqueID = 0;
    //// the technically correct form (Greek cardinal prefixes):
    // electrodeTypes.add("hentrode");
    // electrodeTypes.add("duotrode");
    // electrodeTypes.add("triode");
    // electrodeTypes.add("tetrode");
    // electrodeTypes.add("pentrode");
    // electrodeTypes.add("hextrode");
    // electrodeTypes.add("heptrode");
    // electrodeTypes.add("octrode");
    // electrodeTypes.add("enneatrode");
    // electrodeTypes.add("decatrode");
    // electrodeTypes.add("hendecatrode");
    // electrodeTypes.add("dodecatrode");
    // electrodeTypes.add("triskaidecatrode");
    // electrodeTypes.add("tetrakaidecatrode");
    // electrodeTypes.add("pentakaidecatrode");
    // electrodeTypes.add("hexadecatrode");
    // electrodeTypes.add("heptakaidecatrode");
    // electrodeTypes.add("octakaidecatrode");
    // electrodeTypes.add("enneakaidecatrode");
    // electrodeTypes.add("icosatrode");

    for (int i = 0; i < electrodeTypes.size()+1; i++)
    {
        electrodeCounter.add(0);
    }

    spikeBuffer = new uint8_t[MAX_SPIKE_BUFFER_LEN]; // MAX_SPIKE_BUFFER_LEN defined in SpikeObject.h
	channelBuffers=nullptr;
	PCAbeforeBoxes = true;
}

SpikeDetector::~SpikeDetector()
{

}



AudioProcessorEditor* SpikeDetector::createEditor()
{
    editor = new SpikeDetectorEditor(this, true);
	
    return editor;
}

void SpikeDetector::updateSettings()
{
	 
	mut.enter();
	int numChannels = getNumInputs();
    if (numChannels > 0)
        overflowBuffer.setSize(getNumInputs(), overflowBufferSize);

	if (channelBuffers != nullptr)
		delete channelBuffers;

	double SamplingRate = getSampleRate();;
	double ContinuousBufferLengthSec = 5;
	channelBuffers = new ContinuousCircularBuffer(numChannels,SamplingRate,1, ContinuousBufferLengthSec);
	 
	

	// delete existing histograms
	
	/*
	channelHistograms.clear();
	// create new histograms
	for (int k=0;k<numChannels;k++)
	{
		channelHistograms.add(new Histogram(-500,500,5, true));
	}
	*/

    for (int i = 0; i < electrodes.size(); i++)
    {

        Channel* ch = new Channel(this, i);
        ch->isEventChannel = true;
        ch->eventType = SPIKE_BASE_CODE + electrodes[i]->numChannels;
        ch->name = electrodes[i]->name;

        eventChannels.add(ch);
    }
	mut.exit();
}

/*
Electrode::Electrode(PCAcomputingThread *pth)
{
	spikePlot = nullptr;
	computingThread = pth;
}*/

Electrode::Electrode(int ID, PCAcomputingThread *pth, String _name, int _numChannels, int *_channels, float default_threshold, int pre, int post, float samplingRate )
{
	electrodeID = ID;
		computingThread = pth;

	name = _name;

    numChannels = _numChannels;
    prePeakSamples = pre;
    postPeakSamples = post;

    thresholds = new double[numChannels];
    isActive = new bool[numChannels];
    channels = new int[numChannels];
	channelsDepthOffset = new float[numChannels];

	advancerID = "Unassigned";

    for (int i = 0; i < numChannels; i++)
    {
		channelsDepthOffset[i] = 0;
        channels[i] = _channels[i];
		thresholds[i] = default_threshold;
		isActive[i] = true;
    }
	spikePlot = nullptr;
	spikeSort = new SpikeSortBoxes(computingThread,numChannels, samplingRate, pre+post);
}

bool SpikeDetector::addElectrode(int nChans)
{

	mut.enter();
    int firstChan;

    if (electrodes.size() == 0)
    {
        firstChan = 0;
    }
    else
    {
        Electrode* e = electrodes.getLast();
        firstChan = *(e->channels+(e->numChannels-1))+1;
    }

    if (firstChan + nChans > getNumInputs())
    {
		mut.exit();
        return false;
    }

    int currentVal = electrodeCounter[nChans];
    electrodeCounter.set(nChans,++currentVal);

    String electrodeName;

    // hard-coded for tetrode configuration
    if (nChans < 3)
        electrodeName = electrodeTypes[nChans-1];
    else
        electrodeName = electrodeTypes[nChans-2];

    String newName = electrodeName.substring(0,1);
    newName = newName.toUpperCase();
    electrodeName = electrodeName.substring(1,electrodeName.length());
    newName += electrodeName;
    newName += " ";
    newName += electrodeCounter[nChans];
	
	int *channels = new int[nChans];
	for (int k=0;k<nChans;k++)
		channels[k] = firstChan+k;

	Electrode* newElectrode = new Electrode(++uniqueID,&computingThread,newName, nChans,channels, getDefaultThreshold(), 8,32, getSampleRate());

	String log = "Added electrode (ID "+String(uniqueID)+") with " + String(nChans) + " channels." ;
    std::cout <<log << std::endl;
	String eventlog = "NewElectrode "+String(uniqueID) + " "+String(nChans)+" ";
	for (int k=0;k<nChans;k++)
		eventlog += String(channels[k])+ " ";

	addNetworkEventToQueue(StringTS(eventlog));

    resetElectrode(newElectrode);

    electrodes.add(newElectrode);
	setCurrentElectrodeIndex(electrodes.size()-1);
	mut.exit();
    return true;

}

float SpikeDetector::getDefaultThreshold()
{
    return -20.0f;
}

StringArray SpikeDetector::getElectrodeNames()
{
    StringArray names;
	mut.enter();
    for (int i = 0; i < electrodes.size(); i++)
    {
        names.add(electrodes[i]->name);
    }
	mut.exit();
    return names;
}

void SpikeDetector::resetElectrode(Electrode* e)
{
    e->lastBufferIndex = 0;
}

bool SpikeDetector::removeElectrode(int index)
{
	mut.enter();
    // std::cout << "Spike detector removing electrode" << std::endl;

    if (index > electrodes.size() || index < 0) {
        mut.exit();
		return false;
	}

	
	String log = "Removing electrode (ID " + String(electrodes[index]->electrodeID)+")";
	std::cout << log <<std::endl;

	String eventlog = "RemovingElectrode " + String(electrodes[index]->electrodeID);
	addNetworkEventToQueue(StringTS(eventlog));
	//getUIComponent()->getLogWindow()->addLineToLog(log);

    electrodes.remove(index);
	if (electrodes.size() > 0)
		currentElectrode = electrodes.size()-1;
	else
		currentElectrode = -1;
	
	mut.exit();
    return true;
}

void SpikeDetector::setElectrodeName(int index, String newName)
{
	mut.enter();
    electrodes[index-1]->name = newName;
	mut.exit();
}

void SpikeDetector::setChannel(int electrodeIndex, int channelNum, int newChannel)
{
	mut.enter();
	String log = "Setting electrode " + String(electrodeIndex) + " channel " + String( channelNum )+
              " to " + String( newChannel );
    std::cout << log<< std::endl;

    *(electrodes[electrodeIndex]->channels+channelNum) = newChannel;
	mut.exit();
}

int SpikeDetector::getNumChannels(int index)
{
	mut.enter();
    int i=electrodes[index]->numChannels;
	mut.exit();
	return i;
}

int SpikeDetector::getChannel(int index, int i)
{
	mut.enter();
    int ii=*(electrodes[index]->channels+i);
	mut.exit();
	return ii;
}


void SpikeDetector::setChannelActive(int electrodeIndex, int subChannel, bool active)
{

    currentElectrode = electrodeIndex;
    currentChannelIndex = subChannel;

    if (active)
        setParameter(98, 1);
    else
        setParameter(98, 0);
	
	//getEditorViewport()->makeEditorVisible(this, true, true);
}

bool SpikeDetector::isChannelActive(int electrodeIndex, int i)
{
	mut.enter();
	bool b= *(electrodes[electrodeIndex]->isActive+i);
	mut.exit();
	return b;
}


void SpikeDetector::setChannelThreshold(int electrodeNum, int channelNum, float thresh)
{
	mut.enter();
    currentElectrode = electrodeNum;
    currentChannelIndex = channelNum;
	electrodes[electrodeNum]->thresholds[channelNum] = thresh;
	if (electrodes[electrodeNum]->spikePlot != nullptr)
		electrodes[electrodeNum]->spikePlot->setDisplayThresholdForChannel(channelNum,thresh);
	mut.exit();
    setParameter(99, thresh);
}

double SpikeDetector::getChannelThreshold(int electrodeNum, int channelNum)
{
    mut.enter();
	double f= *(electrodes[electrodeNum]->thresholds+channelNum);
	mut.exit();
	return f;
}

void SpikeDetector::setParameter(int parameterIndex, float newValue)
{
    //editor->updateParameterButtons(parameterIndex);
	mut.enter();
    if (parameterIndex == 99 && currentElectrode > -1)
    {
        *(electrodes[currentElectrode]->thresholds+currentChannelIndex) = newValue;
    }
    else if (parameterIndex == 98 && currentElectrode > -1)
    {
        if (newValue == 0.0f)
            *(electrodes[currentElectrode]->isActive+currentChannelIndex) = false;
        else
            *(electrodes[currentElectrode]->isActive+currentChannelIndex) = true;
    }
	mut.exit();
}


bool SpikeDetector::enable()
{

    useOverflowBuffer = false;
	SpikeDetectorEditor* editor = (SpikeDetectorEditor*) getEditor();
	 editor->enable();
	
    return true;
}

Electrode* SpikeDetector::getActiveElectrode()
{
if (electrodes.size() == 0)
	return nullptr;

return electrodes[currentElectrode];
}

bool SpikeDetector::disable()
{
	mut.enter();
    for (int n = 0; n < electrodes.size(); n++)
    {
        resetElectrode(electrodes[n]);
    }
	editor->disable();
	mut.exit();
    return true;
}

void SpikeDetector::addSpikeEvent(SpikeObject* s, MidiBuffer& eventBuffer, int peakIndex)
{

    // std::cout << "Adding spike event for index " << peakIndex << std::endl;
	
    s->eventType = SPIKE_EVENT_CODE;

    int numBytes = packSpike(s,                        // SpikeObject
                             spikeBuffer,              // uint8_t*
                             MAX_SPIKE_BUFFER_LEN);    // int

    if (numBytes > 0)
        eventBuffer.addEvent(spikeBuffer, numBytes, peakIndex);
    
    //std::cout << "Adding spike" << std::endl;
}

void SpikeDetector::addWaveformToSpikeObject(SpikeObject* s,
                                             int& peakIndex,
                                             int& electrodeNumber,
                                             int& currentChannel)
{
	mut.enter();
    int spikeLength = electrodes[electrodeNumber]->prePeakSamples +
                      + electrodes[electrodeNumber]->postPeakSamples;
	
    s->timestamp = hardware_timestamp + peakIndex;

	// convert sample offset to software ticks
	juce::Time timer;
	int64 ticksPerSec = timer.getHighResolutionTicksPerSecond();
	float samplesPerSec = getSampleRate();
	s->timestamp_software = software_timestamp + ticksPerSec*float(peakIndex)/samplesPerSec;
    s->nSamples = spikeLength;

    int chan = *(electrodes[electrodeNumber]->channels+currentChannel);

    s->gain[currentChannel] = (int)(1.0f / channels[chan]->bitVolts)*1000;
    s->threshold[currentChannel] = (int) electrodes[electrodeNumber]->thresholds[currentChannel]; // / channels[chan]->bitVolts * 1000;

    // cycle through buffer

    if (isChannelActive(electrodeNumber, currentChannel))
    {

        for (int sample = 0; sample < spikeLength; sample++)
        { 

            // warning -- be careful of bitvolts conversion
            s->data[currentIndex] = uint16(getNextSample(electrodes[electrodeNumber]->channels[currentChannel]) / channels[chan]->bitVolts + 32768);

            currentIndex++;
            sampleIndex++;

            //std::cout << currentIndex << std::endl;

        }
    }
    else
    {
        for (int sample = 0; sample < spikeLength; sample++)
        {

            // insert a blank spike if the
            s->data[currentIndex] = 0;
            currentIndex++;
            sampleIndex++;

            //std::cout << currentIndex << std::endl;

        }
    }


    sampleIndex -= spikeLength; // reset sample index
	mut.exit();

}


uint64 SpikeDetector::getExtrapolatedHardwareTimestamp(uint64 softwareTS)
{
	Time timer;
	// this is the case in which messages arrived before the data stream started....
	if (hardware_timestamp == 0) 
		return 0;

	// compute how many ticks passed since the last known software-hardware pair
	int64 ticksPassed = software_timestamp-softwareTS;
	float secondPassed = (float)ticksPassed / timer.getHighResolutionTicksPerSecond();
	// adjust hardware stamp accordingly
	return hardware_timestamp + secondPassed*getSampleRate();
}




void SpikeDetector::postTimestamppedStringToMidiBuffer(StringTS s, MidiBuffer& events)
{
	uint8* msg_with_ts = new uint8[s.len+8]; // for the two timestamps
	memcpy(msg_with_ts, s.str, s.len);	
	memcpy(msg_with_ts+s.len, &s.timestamp, 8);

	addEvent(events, NETWORK,0,0,GENERIC_EVENT,s.len+8,msg_with_ts);
	delete msg_with_ts;
}

void SpikeDetector::handleEvent(int eventType, MidiMessage& event, int sampleNum)
{

    if (eventType == TIMESTAMP)
	{
        const uint8* dataptr = event.getRawData();
	      memcpy(&hardware_timestamp, dataptr + 4, 8); // remember to skip first four bytes
		  memcpy(&software_timestamp, dataptr + 12, 8); // remember to skip first four bytes
    }
}

void SpikeDetector::addNetworkEventToQueue(StringTS S)
{
	StringTS copy(S);
	getUIComponent()->getLogWindow()->addLineToLog(S.getString());
	eventQueue.push(S);
}


void SpikeDetector::postEventsInQueue(MidiBuffer& events)
{
	while (eventQueue.size() > 0)
	{
		StringTS msg = eventQueue.front();
		postTimestamppedStringToMidiBuffer(msg,events);
		eventQueue.pop();
	}
}

void SpikeDetector::process(AudioSampleBuffer& buffer,
                            MidiBuffer& events,
                            int& nSamples)
{
	mut.enter();
	uint16_t samplingFrequencyHz = 30000;//buffer.getSamplingFrequency();
    // cycle through electrodes
    Electrode* electrode;
    dataBuffer = buffer;
	
	
    checkForEvents(events); // find latest's packet timestamps
	
	postEventsInQueue(events);

	channelBuffers->update(buffer,  hardware_timestamp,software_timestamp);
    //std::cout << dataBuffer.getMagnitude(0,nSamples) << std::endl;

    for (int i = 0; i < electrodes.size(); i++)
    {

        //  std::cout << "ELECTRODE " << i << std::endl;

        electrode = electrodes[i];

        // refresh buffer index for this electrode
        sampleIndex = electrode->lastBufferIndex - 1; // subtract 1 to account for
        // increment at start of getNextSample()

        // cycle through samples
        while (samplesAvailable(nSamples))
        {

            sampleIndex++;


            // cycle through channels
            for (int chan = 0; chan < electrode->numChannels; chan++)
            {

                // std::cout << "  channel " << chan << std::endl;

                if (*(electrode->isActive+chan))
                {

                    int currentChannel = electrode->channels[chan];

					bool bSpikeDetectedPositive  = electrode->thresholds[chan] > 0 &&
						(-getNextSample(currentChannel) > electrode->thresholds[chan]); // rising edge
					bool bSpikeDetectedNegative = electrode->thresholds[chan] < 0 &&
						(-getNextSample(currentChannel) < electrode->thresholds[chan]); // falling edge

                    if  (bSpikeDetectedPositive || bSpikeDetectedNegative)
                    { 

                        //std::cout << "Spike detected on electrode " << i << std::endl;
                        // find the peak
                        int peakIndex = sampleIndex;

						if (bSpikeDetectedPositive) 
						{
							// find localmaxima
							while (-getCurrentSample(currentChannel) <
								-getNextSample(currentChannel) &&
								   sampleIndex < peakIndex + electrode->postPeakSamples)
							 {
							 sampleIndex++;
							}
						} else {
							// find local minimum
							
							while (-getCurrentSample(currentChannel) >
								-getNextSample(currentChannel) &&
								   sampleIndex < peakIndex + electrode->postPeakSamples)
							 {
							 sampleIndex++;
							}
						}

                        peakIndex = sampleIndex;
                        sampleIndex -= (electrode->prePeakSamples+1);

                        SpikeObject newSpike;
                        newSpike.timestamp = peakIndex;
						newSpike.electrodeID = electrode->electrodeID;
                        newSpike.source = i;
                        newSpike.nChannels = electrode->numChannels;
						newSpike.samplingFrequencyHz = samplingFrequencyHz;
						newSpike.color[0] = newSpike.color[1] = newSpike.color[2] = 127;
                        currentIndex = 0;

                        // package spikes;
                        for (int channel = 0; channel < electrode->numChannels; channel++)
                        {

                            addWaveformToSpikeObject(&newSpike,
                                                     peakIndex,
                                                     i,
                                                     channel);

                            // if (*(electrode->isActive+currentChannel))
                            // {

                            //     createSpikeEvent(peakIndex,       // peak index
                            //                      i,               // electrodeNumber
                            //                      currentChannel,  // channel number
                            //                      events);         // event buffer


                            // } // end if channel is active

                        }

                       //for (int xxx = 0; xxx < 1000; xxx++) // overload with spikes for testing purposes
						electrode->spikeSort->projectOnPrincipalComponents(&newSpike);

						// Add spike to drawing buffer....
						electrode->spikeSort->sortSpike(&newSpike, PCAbeforeBoxes);

						  // transfer buffered spikes to spike plot
						if (electrode->spikePlot != nullptr) {
							if (electrode->spikeSort->isPCAfinished()) 
							{
								electrode->spikeSort->resetJobStatus();
								float p1min,p2min, p1max,  p2max;
								electrode->spikeSort->getPCArange(p1min,p2min, p1max,  p2max);
								electrode->spikePlot->setPCARange(p1min,p2min, p1max,  p2max);
							}


							electrode->spikePlot->processSpikeObject(newSpike);
						}

//						editor->addSpikeToBuffer(newSpike);
                            addSpikeEvent(&newSpike, events, peakIndex);

                        // advance the sample index
                        sampleIndex = peakIndex + electrode->postPeakSamples;

                        break; // quit spike "for" loop
                    } // end spike trigger

                } // end if channel is active
            } // end cycle through channels on electrode


        } // end cycle through samples

        electrode->lastBufferIndex = sampleIndex - nSamples; // should be negative

        //jassert(electrode->lastBufferIndex < 0);

    } // end cycle through electrodes

    // copy end of this buffer into the overflow buffer

    //std::cout << "Copying buffer" << std::endl;
    // std::cout << "nSamples: " << nSamples;
    //std::cout << "overflowBufferSize:" << overflowBufferSize;

    //std::cout << "sourceStartSample = " << nSamples-overflowBufferSize << std::endl;
    // std::cout << "numSamples = " << overflowBufferSize << std::endl;
    // std::cout << "buffer size = " << buffer.getNumSamples() << std::endl;

    if (nSamples > overflowBufferSize)
    {

        for (int i = 0; i < overflowBuffer.getNumChannels(); i++)
        {

            overflowBuffer.copyFrom(i, 0,
                                    buffer, i,
                                    nSamples-overflowBufferSize,
                                    overflowBufferSize);

            useOverflowBuffer = true;
        }

    }
    else
    {

        useOverflowBuffer = false;
    }


	mut.exit();
}

float SpikeDetector::getNextSample(int& chan)
{



    //if (useOverflowBuffer)
    //{
    if (sampleIndex < 0)
    {
        // std::cout << "  sample index " << sampleIndex << "from overflowBuffer" << std::endl;
        int ind = overflowBufferSize + sampleIndex;

        if (ind < overflowBuffer.getNumSamples())
            return *overflowBuffer.getSampleData(chan, ind);
        else
            return 0;

    }
    else
    {
        //  useOverflowBuffer = false;
        // std::cout << "  sample index " << sampleIndex << "from regular buffer" << std::endl;

        if (sampleIndex < dataBuffer.getNumSamples())
            return *dataBuffer.getSampleData(chan, sampleIndex);
        else
            return 0;
    }
    //} else {
    //    std::cout << "  sample index " << sampleIndex << "from regular buffer" << std::endl;
    //     return *dataBuffer.getSampleData(chan, sampleIndex);
    //}

}

float SpikeDetector::getCurrentSample(int& chan)
{

    // if (useOverflowBuffer)
    // {
    //     return *overflowBuffer.getSampleData(chan, overflowBufferSize + sampleIndex - 1);
    // } else {
    //     return *dataBuffer.getSampleData(chan, sampleIndex - 1);
    // }

    if (sampleIndex < 1)
    {
        //std::cout << "  sample index " << sampleIndex << "from overflowBuffer" << std::endl;
        return *overflowBuffer.getSampleData(chan, overflowBufferSize + sampleIndex - 1);
    }
    else
    {
        //  useOverflowBuffer = false;
        // std::cout << "  sample index " << sampleIndex << "from regular buffer" << std::endl;
        return *dataBuffer.getSampleData(chan, sampleIndex - 1);
    }
    //} else {

}


bool SpikeDetector::samplesAvailable(int& nSamples)
{

    if (sampleIndex > nSamples - overflowBufferSize/2)
    {
        return false;
    }
    else
    {
        return true;
    }

}


void SpikeDetector::saveCustomParametersToXml(XmlElement* parentElement)
{

    for (int i = 0; i < electrodes.size(); i++)
    {
        XmlElement* electrodeNode = parentElement->createNewChildElement("ELECTRODE");
        electrodeNode->setAttribute("name", electrodes[i]->name);
        electrodeNode->setAttribute("numChannels", electrodes[i]->numChannels);
        electrodeNode->setAttribute("prePeakSamples", electrodes[i]->prePeakSamples);
        electrodeNode->setAttribute("postPeakSamples", electrodes[i]->postPeakSamples);

        for (int j = 0; j < electrodes[i]->numChannels; j++)
        {
            XmlElement* channelNode = electrodeNode->createNewChildElement("SUBCHANNEL");
            channelNode->setAttribute("ch",*(electrodes[i]->channels+j));
            channelNode->setAttribute("thresh",*(electrodes[i]->thresholds+j));
            channelNode->setAttribute("isActive",*(electrodes[i]->isActive+j));

        }
    }


}

void SpikeDetector::loadCustomParametersFromXml()
{

    if (parametersAsXml != nullptr)
    {
        // use parametersAsXml to restore state

        int electrodeIndex = -1;

        forEachXmlChildElement(*parametersAsXml, xmlNode)
        {
            if (xmlNode->hasTagName("ELECTRODE"))
            {

                electrodeIndex++;

                int channelsPerElectrode = xmlNode->getIntAttribute("numChannels");

                SpikeDetectorEditor* sde = (SpikeDetectorEditor*) getEditor();
                sde->addElectrode(channelsPerElectrode);

                setElectrodeName(electrodeIndex+1, xmlNode->getStringAttribute("name"));

                int channelIndex = -1;

                forEachXmlChildElement(*xmlNode, channelNode)
                {
                    if (channelNode->hasTagName("SUBCHANNEL"))
                    {
                        channelIndex++;

                        std::cout << "Subchannel " << channelIndex << std::endl;

                        setChannel(electrodeIndex, channelIndex, channelNode->getIntAttribute("ch"));
                        setChannelThreshold(electrodeIndex, channelIndex, channelNode->getDoubleAttribute("thresh"));
                        setChannelActive(electrodeIndex, channelIndex, channelNode->getBoolAttribute("isActive"));
                    }
               }


            }
        }
    }

    SpikeDetectorEditor* ed = (SpikeDetectorEditor*) getEditor();
    ed->checkSettings();

}



void SpikeDetector::removeSpikePlots()
{
	mut.enter();
    for (int i = 0; i < getNumElectrodes(); i++)
    {
        Electrode *ee = electrodes[i];
		ee->spikePlot = nullptr;
    }
	mut.exit();
}

int SpikeDetector::getNumElectrodes()
{
	mut.enter();
    int i= electrodes.size();
	mut.exit();
	return i;

}

int SpikeDetector::getNumberOfChannelsForElectrode(int i)
{
	mut.enter();
    if (i > -1 && i < electrodes.size())
    {
		Electrode *ee = electrodes[i];
		int ii=ee->numChannels;
		mut.exit();
		return ii;
    } else {
		mut.exit();
        return 0;
    }
}



String SpikeDetector::getNameForElectrode(int i)
{
	mut.enter();
    if (i > -1 && i < electrodes.size())
    {
		Electrode *ee = electrodes[i];
        String s= ee->name;
		mut.exit();
		return s;
    } else {
		mut.exit();
        return " ";
    }
}


void SpikeDetector::addSpikePlotForElectrode(SpikeHistogramPlot* sp, int i)
{
	mut.enter();
    Electrode *ee = electrodes[i];
    ee->spikePlot = sp;
	mut.exit();
}

int SpikeDetector::getCurrentElectrodeIndex()
{
	return currentElectrode;
}

void SpikeDetector::setCurrentElectrodeIndex(int i)
{
	jassert(i >= 0 & i  < electrodes.size());
	currentElectrode = i;
}
/*


Histogram::Histogram(float _minValue, float _maxValue, float _resolution, bool _throwOutsideSamples) :  
	minValue(_minValue), maxValue(_maxValue), resolution(_resolution), throwOutsideSamples(_throwOutsideSamples)
{
	numBins = 1+ abs(maxValue-minValue) / resolution;
	binCounts = new unsigned long[numBins];
	binCenters = new float[numBins];
	float deno = (numBins-1)/abs(maxValue-minValue);
	for (int k=0;k<numBins;k++) 
	{
		binCounts[k] = 0;
		binCenters[k] = minValue + k/deno;
	}
}
//
//Histogram::Histogram(float _minValue, float _maxValue, int _numBins, bool _throwOutsideSamples) :  
//	minValue(_minValue), maxValue(_maxValue), numBins(_numBins), throwOutsideSamples(_throwOutsideSamples)
//{
//	resolution = abs(maxValue-minValue) / numBins ;
//	binCounts = new int[numBins];
//	binCenters = new float[numBins];
//	for (int k=0;k<numBins;k++) 
//	{
//		binCounts[k] = 0;
//		binCenters[k] = minValue + k/(numBins-1)*resolution;
//	}
//
//}

void Histogram::clear()
{
for (int k=0;k<numBins;k++) 
	{
		binCounts[k] = 0;
	}	
}


void Histogram::addSamples(float *Samples, int numSamples) {
	for (int k=0;k<numSamples;k++) 
	{
		int indx = ceil( (Samples[k] - minValue) / (maxValue-minValue) * (numBins-1));
		if (indx >= 0 && indx < numBins)
			binCounts[indx]++;
	}
}

Histogram::~Histogram() 
{
		delete [] binCounts;
		delete [] binCenters;
}

	*/













/***********************************/
/*
circularBuffer::circularBuffer(int NumCh, int NumSamplesToHoldPerChannel, double SamplingRate)
{
            numCh = NumCh;
            samplingRate = SamplingRate;
            Buf.resize(numCh);
			for (int ch=0;ch<numCh;ch++) {
				Buf[ch].resize(NumSamplesToHoldPerChannel);
			}
            BufTS_H.resize(NumSamplesToHoldPerChannel);
            BufTS_S.resize(NumSamplesToHoldPerChannel);
            bufLen = NumSamplesToHoldPerChannel;
            numSamplesInBuf = 0;
            ptr = 0; // points to a valid position in the buffer.
}

circularBuffer::~circularBuffer()
{

}


std::vector<double> circularBuffer::getDataArray(int channel, int N)
{
	std::vector<double> LongArray;
	LongArray.resize(N);
	mut.enter();
           
            int p = ptr - 1;
            for (int k = 0; k < N; k++)
            {
                if (p < 0)
                    p = bufLen - 1;
                LongArray[k] = Buf[channel][p];
                p--;
            }
            mut.exit();
            return LongArray;
}

void circularBuffer::addDataToBuffer(std::vector<std::vector<double>> Data, double SoftwareTS, double HardwareTS)
{
	mut.enter();
	int iNumPoints = Data[0].size();
	for (int k = 0; k < iNumPoints; k++)
	{
		BufTS_H[ptr] = HardwareTS + k;
		BufTS_S[ptr] = SoftwareTS + k / samplingRate;
		for (int ch = 0; ch < numCh; ch++)
		{
			Buf[ch, ptr] = Data[ch, k];
		}
		ptr++;

		if (ptr == bufLen)
		{
			ptr = 0;
		}
		numSamplesInBuf++;
		if (numSamplesInBuf >= bufLen)
		{
			numSamplesInBuf = bufLen;
		}
	}
	mut.exit();
}


double circularBuffer::findThresholdForChannel(int channel)
{
	// Run median on analog input
	double numSamplesPerSecond = 30000;
	std::vector<double> LongArray = getDataArray(channel, numSamplesPerSecond*5);
	
	for (int k = 0; k < LongArray.size(); k++)
		LongArray[k] = fabs(LongArray[k]);

	std::sort (LongArray.begin(), LongArray.begin()+LongArray.size());           //(12 32 45 71)26 80 53 33


	int Middle = LongArray.size() / 2;
	double Median = LongArray[Middle];
	double NewThres = -4.0F * Median / 0.675F;

	return NewThres;
}


/**************************************/

void ContinuousCircularBuffer::reallocate(int NumCh)
{
	numCh =NumCh;
	Buf.resize(numCh);
	for (int k=0;k< numCh;k++)
	{
		Buf[k].resize(bufLen);
	}
	numSamplesInBuf = 0;
	ptr = 0; // points to a valid position in the buffer.

}


ContinuousCircularBuffer::ContinuousCircularBuffer(int NumCh, float SamplingRate, int SubSampling, float NumSecInBuffer)
{
		Time t;
	
	numTicksPerSecond = t.getHighResolutionTicksPerSecond();

	int numSamplesToHoldPerChannel = (SamplingRate * NumSecInBuffer / SubSampling);
	subSampling = SubSampling;
	samplingRate = SamplingRate;
	numCh =NumCh;
	leftover_k = 0;
	Buf.resize(numCh);


	for (int k=0;k< numCh;k++)
	{
		Buf[k].resize(numSamplesToHoldPerChannel);
	}

	hardwareTS.resize(numSamplesToHoldPerChannel);
	softwareTS.resize(numSamplesToHoldPerChannel);
	valid.resize(numSamplesToHoldPerChannel);
	bufLen = numSamplesToHoldPerChannel;
	numSamplesInBuf = 0;
	ptr = 0; // points to a valid position in the buffer.
}

/*
void ContinuousCircularBuffer::FindInterval(int saved_ptr, double Bef, double Aft, int &start, int &N)
{
	int CurrPtr = saved_ptr;
	N = 0;
	while (N < bufLen && N < numSamplesInBuf)
	{
		if ((TS[CurrPtr] < Bef) || (TS[CurrPtr] > Aft) || !valid[CurrPtr])
			break;
		// Add spike..
		CurrPtr--;
		N++;
		if (CurrPtr < 0)
			CurrPtr = bufLen - 1;
	}
	if (N > 0 && !valid[CurrPtr])
	{
		CurrPtr++;
		if (CurrPtr >= bufLen)
			CurrPtr = 0;
	}
	start = CurrPtr;
	CurrPtr = saved_ptr;
	while (N < bufLen && N < numSamplesInBuf)
	{
		if ((TS[CurrPtr] < Bef) || (TS[CurrPtr] > Aft) || !valid[CurrPtr])
			break;
		// Add spike..
		CurrPtr++;
		N++;
		if (CurrPtr >= bufLen)
			CurrPtr = 0;
	}
	if (N > 0 && !valid[CurrPtr])
	{
		CurrPtr--;
		if (CurrPtr < 0)
			CurrPtr = bufLen-1;
	}

}
*/

/*
LFP_Trial_Data ContinuousCircularBuffer::GetRelevantData(int saved_ptr, double Start_TS, double Align_TS, double End_TS, double BeforeSec, double AfterSec)
{
             // gurantee to return to return the first index "start" such that TS[start] < T0-SearchBeforeSec
             // and TS[end] > T0+SearchAfterSec
	mut.enter();
	int N,start;
	FindInterval(saved_ptr, Start_TS - BeforeSec, End_TS + AfterSec, start, N);
	LFP_Trial_Data triallfp(numCh, N);
	int p = start;
	for (int k = 0; k < N; k++)
	{
		triallfp.time[k] = TS[p]-Align_TS;
		for (int ch = 0; ch < numCh; ch++)
		{
			triallfp.data[ch][k] = Buf[ch][p];
		}
		p++;
		if (p >= bufLen)
			p = 0;
	}
	mut.exit();
	return triallfp;
}
*/
void ContinuousCircularBuffer::update(AudioSampleBuffer& buffer, uint64 hardware_ts, uint64 software_ts)
{
	mut.enter();
	int numpts = buffer.getNumSamples();
	
	// we don't start from zero because of subsampling issues.
	// previous packet may not have ended exactly at the last given sample.
	int k = leftover_k;
	for (; k < numpts; k+=subSampling)
	{
		valid[ptr] = true;
		hardwareTS[ptr] = hardware_ts + k;
		softwareTS[ptr] = software_ts + uint64(float(k) / samplingRate * numTicksPerSecond);

		for (int ch = 0; ch < numCh; ch++)
		{
			Buf[ch][ptr] = *(buffer.getSampleData(ch,k));
		}
		ptr++;
		if (ptr == bufLen)
		{
			ptr = 0;
		}
		numSamplesInBuf++;
		if (numSamplesInBuf >= bufLen)
		{
			numSamplesInBuf = bufLen;
		}
	}
	leftover_k =subSampling-( numpts-k);
	mut.exit();

}
/*
void ContinuousCircularBuffer::AddDataToBuffer(std::vector<std::vector<double>> lfp, double soft_ts)
{
	mut.enter();
	int numpts = lfp[0].size();
	for (int k = 0; k < numpts / subSampling; k++)
	{
		valid[ptr] = true;
		for (int ch = 0; ch < numCh; ch++)
		{
			Buf[ch][ptr] = lfp[ch][k];
			TS[ptr] = soft_ts + (double)(k * subSampling) / samplingRate;
		}
		ptr++;
		if (ptr == bufLen)
		{
			ptr = 0;
		}
		numSamplesInBuf++;
		if (numSamplesInBuf >= bufLen)
		{
			numSamplesInBuf = bufLen;
		}
	}
	mut.exit();
}
*/
        
int ContinuousCircularBuffer::GetPtr()
{
	return ptr;
}

/************************************************************/



