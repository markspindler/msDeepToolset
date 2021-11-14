/**
msDeepBlur v1.0.0 (c) by Mark Spindler

msDeepBlur is licensed under a Creative Commons Attribution 3.0 Unported License.

To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/.
**/


static const char* const CLASS = "msDeepBlur";
static const char* const HELP =	"Performs a Gaussian blur on Deep images. Be careful to keep the size of the blur small, as this node can become extremely slow to render for larger sizes! "
                                "Be aware that the number of Deep samples in your image will increase substantially.\n\n"

								"Version: 1.0.0\n"
								"Author: Mark Spindler\n"
								"Contact: info@mark-spindler.com";


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



class msDeepBlur : public DeepOnlyOp
{
	private:
		float _size[2];
		bool _drop_hidden;
		bool _drop_transparent;
		float _threshold;
		bool _volumetric;
		bool _fast_blur;

		int kernel_radius[2];
		int kernel_dimensions[2];
		int amount;
		float sigma[2];

	public:
		int minimum_inputs() const {return 1;}
		int maximum_inputs() const {return 1;}

		msDeepBlur(Node* node) : DeepOnlyOp(node)
		{
			_size [0] = _size[1] = 0;
			_drop_hidden = true;
			_drop_transparent = true;
			_threshold = 0;
			_volumetric = true;
			_fast_blur = false;
		}
	
		virtual void knobs(Knob_Callback);
		int knob_changed(Knob*);
		bool test_input(int, Op*) const;
		void _validate(bool);
		virtual void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&);
		virtual bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&);
		
		DeepOp* input0() {return dynamic_cast<DeepOp*>(Op::input(0));}
	
		const char* Class() const {return CLASS;}
		const char* node_help() const {return HELP;}
		virtual Op* op() {return this;}
		static const Iop::Description d;
};


void msDeepBlur::knobs(Knob_Callback f)
{
	Text_knob(f, "", "Attention: Be careful to keep the size of the blur small, as this node can\nbecome extremely slow to render for larger sizes!");

	Divider(f, "");

	WH_knob(f, _size, "size");
	SetRange(f, 0, 5);

	Divider(f, "");
	
	Bool_knob(f, &_drop_hidden, "drop_hidden", "drop hidden samples");
	Tooltip(f, "Remove samples that are behind others with alpha 1 (i.e. those that are entirely occluded). Depending on the image content, this will make this node and subsequent Deep nodes render faster.");
	SetFlags(f, Knob::STARTLINE);

	Bool_knob(f, &_drop_transparent, "drop_transparent", "drop transparent samples");
	Tooltip(f, "Remove samples with an alpha value equal or smaller than the specified threshold. Depending on the image content, this will make this node and subsequent Deep nodes render faster. A threshold above 0 might slightly change the resulting image.");

	Float_knob(f, &_threshold, "threshold", "threshold");
	Tooltip(f, "If \"drop transparent samples\" is activated, any samples with an alpha value equal or smaller than this threshold will be removed.");
	SetRange(f, 0, 1);
}


int msDeepBlur::knob_changed(Knob* k)
{
	if(k == &Knob::showPanel)
	{
		knob("threshold")->enable(_drop_transparent);
		return 1;
	}

	if(k->is("drop_transparent"))
	{
		knob("threshold")->enable(_drop_transparent);
		return 1;
	}
}


bool msDeepBlur::test_input(int input, Op* op) const
{
	return dynamic_cast<DeepOp*>(op) != 0;
}


void msDeepBlur::_validate(bool for_real)
{
	if (input0())
	{
		input0()->validate(for_real);
		_deepInfo = input0()->deepInfo();
	}

	else
		_deepInfo = DeepInfo();
}


void msDeepBlur::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests)
{
	kernel_radius[0] = std::floor(std::abs(_size[0]) * 1.5f);		//approximation of the relation between size and kernel dimensions in Nuke's Blur node
	kernel_radius[1] = std::floor(std::abs(_size[1]) * 1.5f);
	kernel_dimensions[0] = kernel_radius[0] * 2 + 1;
	kernel_dimensions[1] = kernel_radius[1] * 2 + 1;
	amount = kernel_dimensions[0] * kernel_dimensions[1];
	sigma[0] = _size[0] * 0.425;									//approximation of the relation between size and sigma in Nuke's Blur node
	sigma[1] = _size[1] * 0.425;	


	if (input0())
	{
		Box myBox = box;
		myBox.x(box.x() - kernel_radius[0]);
		myBox.y(box.y() - kernel_radius[1]);
		myBox.r(box.r() + kernel_radius[0]);
		myBox.t(box.t() + kernel_radius[1]);

		requests.push_back(RequestData(input0(), myBox, channels, count));
	}
}


void calculateGaussianMatrix(int kernel_radius[2], float sigma[2], float weight[])
{	
	int amount = (kernel_radius[0] * 2 + 1) * (kernel_radius[1] * 2 + 1);
	float* weight_horizontal = new float[kernel_radius[0] + 1];
	float* weight_vertical = new float[kernel_radius[1] + 1];
	float weight_sum = 0;
	int counter = 0;

	//calculate horizontal weights
	for (int i = 0; i <= kernel_radius[0]; i++)
	{
		if (sigma[0] == 0)
			weight_horizontal[i] = 1;
		else
			weight_horizontal[i] = std::exp((-1.0f)*(i*i) / (2 * sigma[0]*sigma[0])) / std::sqrt(3.14159265358979f * 2 * sigma[0]*sigma[0]);		//Gaussian function
	}

	//calculate vertical weights
	for (int j = 0; j <= kernel_radius[1]; j++)
	{
		if (kernel_radius[0] == kernel_radius[1])
		{
			weight_vertical[j] = weight_horizontal[j];
		}

		else
		{
			if (sigma[1] == 0)
				weight_vertical[j] = 1;
			else
				weight_vertical[j] = std::exp((-1.0f)*(j*j) / (2 * sigma[1]*sigma[1])) / std::sqrt(3.14159265358979f * 2 * sigma[1]*sigma[1]);
		}
	}

	//combine horizontal and vertical weights
	for (int i = -kernel_radius[0]; i <= kernel_radius[0]; i++)
	{
		for (int j = -kernel_radius[1]; j <= kernel_radius[1]; j++)
		{
			float combined_weight = weight_horizontal[std::abs(i)] * weight_vertical[std::abs(j)];
			weight[counter] = combined_weight;
			weight_sum += combined_weight;

			counter++;
		}
	}

	//normalize weights, so their sum equals 1
	weight_sum = 1 / weight_sum;
	for (int i = 0; i < amount; i++)
		weight[i] *= weight_sum;
	

	delete[] weight_horizontal;
	delete[] weight_vertical;
}


bool msDeepBlur::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& outPlane)
{
	if (!input0())
		return false;

	DeepPlane inPlane;

	Box myBox = box;
	myBox.x(box.x() - kernel_radius[0]);
	myBox.y(box.y() - kernel_radius[1]);
	myBox.r(box.r() + kernel_radius[0]);
	myBox.t(box.t() + kernel_radius[1]);

	if (!input0()->deepEngine(myBox, channels, inPlane))
		return false;

	//calculate weights for Gaussian Blur
	float* weight = new float[amount];
	calculateGaussianMatrix(kernel_radius, sigma, weight);

	
	//cycle through all pixels and calculate outcome 
	outPlane = DeepOutputPlane(channels, box);

	for (int y = box.y(); y < box.t(); y++)
	{
		for (int x = box.x(); x < box.r(); x++)
		{	
			//cycle through pixels in convolve area and add them to vector inPixels
			std::vector<DeepPixel> inPixels;

			for (int i = -kernel_radius[0]; i <= kernel_radius[0]; i++)
			{
				for (int j = -kernel_radius[1]; j <= kernel_radius[1]; j++)
				{
					inPixels.push_back(inPlane.getPixel(y + j, x + i));
				}
			}

			//combine pixels in convolve area and output the result
			DeepOutPixel outPixel;
			outPixel.clear();
			combineDeepPixels(inPixels, outPixel, channels, amount, weight, _drop_hidden, _drop_transparent, _threshold);
			outPlane.addPixel(outPixel);
		}
	}

	delete[] weight;

    return true;
}


static Op* build(Node* node) {return new msDeepBlur(node);}
const Op::Description msDeepBlur::d("msDeepBlur", 0, build);