/**
msDeepKeymix v1.0.0 (c) by Mark Spindler

msDeepKeymix is licensed under a Creative Commons Attribution 3.0 Unported License.

To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/.
**/


static const char* const CLASS = "msDeepKeymix";
static const char* const HELP =	"Has the same functionality as a regular KeyMix node, but works with deep images. "
                                "The only other difference is that all channels will be mixed by the given mask, "
                                "i.e. you can't limit the operation to specific channels and pipe the other channels through unchanged.\n\n"

								"Version: 1.0.0\n"
								"Author: Mark Spindler\n"
								"Contact: info@mark-spindler.com";

static const char* const bbox_names[] = {"union", "B side", "A side", 0};


#include <numeric>
#include <math.h>
#include "DDImage/DeepOp.h"
#include "DDImage/Filter.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Pixel.h"
#include "DDImage/RequestData.h"
#include "msDeepFunctions.h"



using namespace DD::Image;



class msDeepKeymix : public DeepOnlyOp
{
	private:
		Channel _mask_channel;
		bool _invert_mask;
		float _mix;
		int _bbox;

	public:
		int minimum_inputs() const {return 3;}
		int maximum_inputs() const {return 3;}

		msDeepKeymix(Node* node) : DeepOnlyOp(node)
		{
			_mask_channel = Chan_Alpha;
			_invert_mask = false;
			_mix = 1;
			_bbox = 0;
		}
	
		virtual void knobs(Knob_Callback);
		const char* input_label (int, char*) const;
		bool test_input(int, Op*) const;		
		virtual Op* default_input(int) const;
		void _validate(bool);
		virtual void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&);
		virtual bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&);
		
		DeepOp* inputB() {return dynamic_cast<DeepOp*>(Op::input(0));}
		DeepOp* inputA() {return dynamic_cast<DeepOp*>(Op::input(1));}
		Iop* inputMask() {return dynamic_cast<Iop*>(Op::input(2));}
	
		const char* Class() const {return CLASS;}
		const char* node_help() const {return HELP;}
		virtual Op* op() {return this;}
		static const Iop::Description d;
};


void msDeepKeymix::knobs(Knob_Callback f)
{
	Input_Channel_knob(f, &_mask_channel, 1, 2, "maskChannel", "mask channel");	//1 -> only 1 channel can be selected; 2 -> shows channels from input0
	Tooltip(f, "Channel to use from mask input");
	Bool_knob(f, &_invert_mask, "invertMask", "invert");
	Tooltip(f, "Flip meaning of the the mask channel");
	Float_knob(f, &_mix, "mix");
	Tooltip(f, "Dissolve between B-only at 0 and the full keymix at 1");
	Enumeration_knob(f, &_bbox, bbox_names, "bbox", "Set BBox to");
	Tooltip(f, "Clip one input to match the other if wanted");
}


const char* msDeepKeymix::input_label(int input, char* buffer) const
{
	switch (input)
	{
		case 0:
			return "B";
		case 1:
			return "A";
		case 2:
			return "mask";
		default:
			return 0;
	}
}


bool msDeepKeymix::test_input(int input, Op* op) const
{
	if (input == 2)
		return dynamic_cast<Iop*>(op) != 0;

	else
		return dynamic_cast<DeepOp*>(op) != 0;
}


Op* msDeepKeymix::default_input(int input) const
{
	if (input == 2)
		return NULL;

	else
		return Op::default_input(input);
}


void msDeepKeymix::_validate(bool for_real)
{
	if (inputB())
	{
		inputB()->validate(for_real);

		if (inputA())
		{
			inputA()->validate(for_real);
			
			Box bbox;
			if (_bbox == 0)
			{
				bbox = inputB()->deepInfo().box();
				bbox.merge(inputA()->deepInfo().box());
			}
			else if (_bbox == 1)
				bbox = inputB()->deepInfo().box();
			else
				bbox = inputA()->deepInfo().box();


			ChannelSet outchans(inputB()->deepInfo().channels());
			outchans += inputA()->deepInfo().channels();

			_deepInfo = DeepInfo(inputB()->deepInfo().formats(), bbox, outchans);
		}

		else
			_deepInfo = inputB()->deepInfo();
	}

	else
		_deepInfo = DeepInfo();
}


void msDeepKeymix::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests)
{
	if (inputB())
	{
		RequestData reqB(inputB(), box, inputB()->deepInfo().channels(), count);
		requests.push_back(reqB);

		if (inputA())
		{
			RequestData reqA(inputA(), box, inputA()->deepInfo().channels(), count);
			requests.push_back(reqA);

			RequestData reqMask(inputMask(), box, _mask_channel, count);
			requests.push_back(reqMask);
		}
	}	
}


bool msDeepKeymix::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& outPlane)
{
	if (!inputB())
		return false;

	DeepPlane inPlaneB;
	DeepPlane inPlaneA;

	if (!inputB()->deepEngine(box, inputB()->deepInfo().channels(), inPlaneB))
		return false;

	outPlane = DeepOutputPlane(channels, box);


	if (inputA())
	{
		if (!inputA()->deepEngine(box, inputA()->deepInfo().channels(), inPlaneA))
			return false;

		for (Box::iterator it = box.begin(); it != box.end(); it++)
		{	
			float mask_value = 0;
			if(inputMask())
			{
				mask_value = clamp(inputMask()->at(it.x, it.y, _mask_channel));
				if (_invert_mask)
					mask_value = 1 - mask_value;
				mask_value *= _mix;
			}

			//if mask channel is 0, simply pipe through input B
			if (mask_value == 0)
			{
				DeepPixel inPixel = inPlaneB.getPixel(it);
				DeepOutPixel outPixel(inPixel.getSampleCount() * channels.size());

				for (int sampleNo = 0; sampleNo < inPixel.getSampleCount(); sampleNo++)
				{
					foreach (z, channels)
					{
						if (inPixel.channels().contains(z))
							outPixel.push_back(inPixel.getUnorderedSample(sampleNo, z));
						else
							outPixel.push_back(0);
					}
				}

				outPlane.addPixel(outPixel);
			}

			//if mask channel is 1, simply pipe through input A
			else if (mask_value == 1)
			{
				DeepPixel inPixel = inPlaneA.getPixel(it);
				DeepOutPixel outPixel(inPixel.getSampleCount() * channels.size());

				for (int sampleNo = 0; sampleNo < inPixel.getSampleCount(); sampleNo++)
				{
					foreach (z, channels)
					{
						if (inPixel.channels().contains(z))
							outPixel.push_back(inPixel.getUnorderedSample(sampleNo, z));
						else
							outPixel.push_back(0);
					}
				}

				outPlane.addPixel(outPixel);
			}

			//if mask channel is between 0 and 1, combine pixels from inputs A and B
			else
			{
				std::vector<DeepPixel> inPixels;
				inPixels.push_back(inPlaneB.getPixel(it.y, it.x));
				inPixels.push_back(inPlaneA.getPixel(it.y, it.x));

				DeepOutPixel outPixel;
				outPixel.clear();

				float weight[2];
				weight[0] = 1 - mask_value;
				weight[1] = mask_value;

				combineDeepPixels(inPixels, outPixel, channels, 2, weight, false, false, 0);
			
				outPlane.addPixel(outPixel);
			}
		}
	}

	else
	{
		for (Box::iterator it = box.begin(); it != box.end(); it++)
		{	
			DeepPixel inPixel = inPlaneB.getPixel(it);
			DeepOutPixel outPixel(inPixel.getSampleCount() * channels.size());

			for (int sampleNo = 0; sampleNo < inPixel.getSampleCount(); sampleNo++)
			{
				foreach (z, channels)
				{
					outPixel.push_back(inPixel.getUnorderedSample(sampleNo, z));
				}
			}

			outPlane.addPixel(outPixel);
		}
	}
	
	return true;
}


static Op* build(Node* node) {return new msDeepKeymix(node);}
const Op::Description msDeepKeymix::d("msDeepKeymix", 0, build);