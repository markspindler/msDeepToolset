/**
msDeepReformat v1.0.0 (c) by Mark Spindler

msDeepReformat is licensed under a Creative Commons Attribution 3.0 Unported License.

To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/.
**/


static const char* const CLASS = "msDeepReformat";
static const char* const HELP =	"Works like Nuke's regular DeepReformat, but uses a cubic filter.\n\n"

								"Version: 1.0.0\n"
								"Author: Mark Spindler\n"
								"Contact: info@mark-spindler.com";

static const char* const types[] = {"to format", "to box", "scale", 0};
static const char* const resize_types[] = {"none", "width", "height", "fit", "fill", "distort", 0};


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



class msDeepReformat : public DeepOnlyOp
{
	private:
		int _type;
		FormatPair _format;
		int _box_width;
		int _box_height;
		bool _box_fixed;
		float _box_pixel_aspect;
		double _scale[2];		
		int _resize_type;
		bool _center;
		bool _preserve_bbox;
		bool _drop_hidden;
		bool _drop_transparent;
		float _threshold;

		Matrix4 matrix;
		float scale_factor[2];

		FormatPair formats;
		Format format;
		Format full_size_format;

		enum {to_format, to_box, scale};
		enum {none, width, height, fit, fill, distort};


	public:
		int minimum_inputs() const {return 1;}
		int maximum_inputs() const {return 1;}

		msDeepReformat(Node* node) : DeepOnlyOp(node)
		{
			_type = to_format;
			_format.format(0);
			_box_width = 200;
			_box_height = 200;
			_box_fixed = false;
			_box_pixel_aspect = 1;
			_scale[0] = _scale[1] = 1;
			_resize_type = width;
			_center = true;
			_preserve_bbox = false;
			_drop_hidden = true;
			_drop_transparent = true;
			_threshold = 0;
		}
	
		virtual void knobs(Knob_Callback);
		int knob_changed(Knob*);
		bool test_input(int, Op*) const;		
		virtual Op* default_input(int) const;
		void _validate(bool);
		virtual void getDeepRequests(Box, const ChannelSet&, int, std::vector<RequestData>&);
		void calculateMatrix();
		virtual bool doDeepEngine(Box, const ChannelSet&, DeepOutputPlane&);

		DeepOp* input0() {return dynamic_cast<DeepOp*>(Op::input(0));}
	
		const char* Class() const {return CLASS;}
		const char* node_help() const {return HELP;}
		virtual Op* op() {return this;}
		static const Iop::Description d;
};


void msDeepReformat::knobs(Knob_Callback f)
{	
	Enumeration_knob(f, &_type, types, "type", "type");
	Tooltip(f,  "to format: Convert between formats. The \"image area\" of the input format is resized to fit the image area of the output format, and differences in pixel aspect ratios are handled.\n\n"
				"to box: Scale to fit inside or fill a box measured in pixels. This is useful for making postage-stamp images.\n\n"
				"scale: Scale the image. The scale factor will be rounded slightly so that the output image is an integer number of pixels in the direction chosen by \"resize type\".");
	
	Format_knob(f, &_format, "format", "output format");

	Int_knob(f, &_box_width, "box_width", "width/height");
	Int_knob(f, &_box_height, "box_height", "");
	ClearFlags(f, Knob::STARTLINE);
	Bool_knob(f, &_box_fixed, "box_fixed", "force this shape");
	Tooltip(f, "If checked the output is exactly this shape, with one direction either clipped or padded. If this is not checked the output image is approximately the same shape as the input, round to the nearest integer, this is useful for making postage-stamp images.");
	Float_knob(f, &_box_pixel_aspect, "box_pixel_aspect", "pixel aspect");
	ClearFlags(f, Knob::SLIDER);

	Scale_knob(f, _scale, "scale", "scale");
	Tooltip(f,	"If you select the [2] button you can scale each direction differently. You should change resize type to \"distort\".");
	SetRange(f, 0.1, 10);

	Divider(f,"");

	Enumeration_knob(f, &_resize_type, resize_types, "resize", "resize type");
	Tooltip(f,	"Choose which direction controls the scaling factor:\n"
				"none: don't change the pixels\n"
				"width: scale so it fills the output width\n"
				"height: scale so it fills the output height\n"
				"fit: smaller of width or height\n"
				"fill: larger of width or height\n"
				"distort: non-uniform scale to match both width and height");
	Bool_knob(f, &_center, "center", "center");
	Tooltip(f,	"Translate the image to center it in the output. If off, it is translated so the lower-left corners are lined up.");
	Bool_knob(f, &_preserve_bbox, "pbb", "preserve bounding box");
	Tooltip(f,	"Normally any pixels outside the output format are clipped off, as this matches what will be written to most image files. Turn this on to preserve them.");

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


int msDeepReformat::knob_changed(Knob* k)
{
	if (k == &Knob::showPanel)
	{
		knob("format")->visible(_type == to_format);
		knob("box_width")->visible(_type == to_box);
		knob("box_height")->visible(_type == to_box);
		knob("box_fixed")->visible(_type == to_box);
		knob("box_pixel_aspect")->visible(_type == to_box);
		knob("scale")->visible(_type == scale);

		knob("box_height")->enable(_box_fixed);
		
		knob("threshold")->enable(_drop_transparent);

		return 1;
	}

	if(k->is("type"))
	{
		knob("format")->visible(_type == to_format);
		knob("box_width")->visible(_type == to_box);
		knob("box_height")->visible(_type == to_box);
		knob("box_fixed")->visible(_type == to_box);
		knob("box_pixel_aspect")->visible(_type == to_box);
		knob("scale")->visible(_type == scale);

		return 1;
	}
	
	if (k->is("box_fixed"))
	{
		knob("box_height")->enable(_box_fixed);
		
		return 1;
	}

	if(k->is("drop_transparent"))
	{
		knob("threshold")->enable(_drop_transparent);
		return 1;
	}
}


bool msDeepReformat::test_input(int input, Op* op) const
{
	return dynamic_cast<DeepOp*>(op) != 0;
}


Op* msDeepReformat::default_input(int input) const
{
	return Op::default_input(input);
}


void msDeepReformat::_validate(bool for_real)
{
	if (input0())
	{
		input0()->validate(for_real);

		calculateMatrix();
		Matrix4 matrix_inverted = matrix.inverse();

		Vector2 bottom_left(input0()->deepInfo().x(), input0()->deepInfo().y());
		Vector2 top_right(input0()->deepInfo().r(), input0()->deepInfo().t());
		bottom_left = matrix_inverted.transform(bottom_left);
		top_right = matrix_inverted.transform(top_right);
		
		Box myBox;
		myBox.x(floor(std::min(bottom_left.x, top_right.x)));
		myBox.y(floor(std::min(bottom_left.y, top_right.y)));
		myBox.r(ceil(std::max(bottom_left.x, top_right.x)));
		myBox.t(ceil(std::max(bottom_left.y, top_right.y)));

		if (!_preserve_bbox)
			myBox.intersect(formats.format()->x(), formats.format()->y(), formats.format()->r(), formats.format()->t());

		_deepInfo = input0()->deepInfo();
		_deepInfo.setFormats(formats);
		_deepInfo.setBox(myBox);
	}

	else
		_deepInfo = DeepInfo();
}


void msDeepReformat::getDeepRequests(Box box, const ChannelSet& channels, int count, std::vector<RequestData>& requests)
{
	Vector2 bottom_left(input_format().x(), input_format().y());
	Vector2 top_right(input_format().r(), input_format().t());
	bottom_left = matrix.transform(bottom_left);
	top_right = matrix.transform(top_right);

	Box myBox = box;
	myBox.x(floor(std::min(bottom_left.x, top_right.x)) - ceil(scale_factor[0]));
	myBox.y(floor(std::min(bottom_left.y, top_right.y)) - ceil(scale_factor[1]));
	myBox.r(ceil(std::max(bottom_left.x, top_right.x)) + ceil(scale_factor[0]));
	myBox.t(ceil(std::max(bottom_left.y, top_right.y)) + ceil(scale_factor[1]));

	requests.push_back(RequestData(input0(), myBox, channels, count));
}


void msDeepReformat::calculateMatrix()
{
	DeepInfo x = input0()->deepInfo();
	float w = x.format()->width();
	float h = x.format()->height();


	//calculate the scaling factor
	if (_type == to_format)
	{
		scale_factor[0] = w / _format.format()->width();
		scale_factor[1] = h / _format.format()->height();

		formats = _format;
	}

	else if (_type == to_box) 
	{

		scale_factor[0] =  w / _box_width;

		format.width(_box_width);
		format.pixel_aspect(_box_pixel_aspect);
		full_size_format.width(_box_width);
		full_size_format.pixel_aspect(_box_pixel_aspect);

		if (_box_fixed)
		{
			scale_factor[1] = h / _box_height;

			format.height(_box_height);
			full_size_format.height(_box_height);
		}

		else
		{
			scale_factor[1] = scale_factor[0];

			format.height(_box_width * h / w);
			full_size_format.height(_box_width * h / w);
		}
		
		format.set(0, 0, format.width(), format.height());
		full_size_format.set(0, 0, format.width(), format.height());
		formats.format(&format);
		formats.fullSizeFormat(&full_size_format);
	}

	else //type = scale
	{
		scale_factor[0] = 1 / _scale[0];
		scale_factor[1] = 1 / _scale[1];

		format.width(w / scale_factor[0]);
		format.height(h / scale_factor[1]);
		format.pixel_aspect(x.format()->pixel_aspect());
		format.set(0, 0, format.width(), format.height());

		full_size_format.width(w / scale_factor[0]);
		full_size_format.height(h / scale_factor[1]);
		full_size_format.pixel_aspect(x.format()->pixel_aspect());
		full_size_format.set(0, 0, format.width(), format.height());
		
		formats.format(&format);
		formats.fullSizeFormat(&full_size_format);
	}


	if (_resize_type == none)
	{
		matrix.makeIdentity();
		if (_center)
			matrix.translate((int)(w * 0.5) - formats.format()->center_x(), (int)(h * 0.5) - formats.format()->center_y());
	}

	else
	{
		//take resize type into account
		if (_resize_type == width)
		{
			scale_factor[1] = scale_factor[0];

			if (formats.format()->pixel_aspect() != x.format()->pixel_aspect())
				scale_factor[1] = scale_factor[1] * x.format()->pixel_aspect() / formats.format()->pixel_aspect();
		}
	
		else if (_resize_type == height)
		{
			scale_factor[0] = scale_factor[1];

			if (formats.format()->pixel_aspect() != x.format()->pixel_aspect())
				scale_factor[0] = scale_factor[0] * formats.format()->pixel_aspect() / x.format()->pixel_aspect();
		}

		else if (_resize_type == fit)
		{
			if (formats.format()->pixel_aspect() == x.format()->pixel_aspect())
			{
				scale_factor[0] = scale_factor[1] = std::max(scale_factor[0], scale_factor[1]);
			}

			else
			{
				if (scale_factor[0] > scale_factor[1] * formats.format()->pixel_aspect() / x.format()->pixel_aspect())
					scale_factor[1] = scale_factor[0] * x.format()->pixel_aspect() / formats.format()->pixel_aspect();

				else
					scale_factor[0] = scale_factor[1] * formats.format()->pixel_aspect() / x.format()->pixel_aspect();
			}
		}
	
		else if (_resize_type == fill)
		{
			if (formats.format()->pixel_aspect() == x.format()->pixel_aspect())
			{
				scale_factor[0] = scale_factor[1] = std::min(scale_factor[0], scale_factor[1]);
			}

			else
			{
				if (scale_factor[0] < scale_factor[1] * formats.format()->pixel_aspect() / x.format()->pixel_aspect())
					scale_factor[1] = scale_factor[0] * x.format()->pixel_aspect() / formats.format()->pixel_aspect();

				else
					scale_factor[0] = scale_factor[1] * formats.format()->pixel_aspect() / x.format()->pixel_aspect();
			}
		}

		matrix.makeIdentity();
		matrix.translate(-0.5, -0.5);
		if (_center)
			matrix.translate(w * 0.5, h * 0.5);
		matrix.scale(scale_factor[0], scale_factor[1]);
		if (_center)
			matrix.translate(-formats.format()->center_x(), -formats.format()->center_y());
		matrix.translate(0.5, 0.5);
	}
}


bool msDeepReformat::doDeepEngine(Box box, const ChannelSet& channels, DeepOutputPlane& outPlane)
{
	if (!input0())
		return false;

	DeepPlane inPlane;

	Vector2 bottom_left(box.x(), box.y());
	Vector2 top_right(box.r(), box.t());
	bottom_left = matrix.transform(bottom_left);
	top_right = matrix.transform(top_right);
	
	Box myBox;
	myBox.x(floor(std::min(bottom_left.x, top_right.x)) - ceil(scale_factor[0]));
	myBox.y(floor(std::min(bottom_left.y, top_right.y)) - ceil(scale_factor[1]));
	myBox.r(ceil(std::max(bottom_left.x, top_right.x)) + ceil(scale_factor[0]));
	myBox.t(ceil(std::max(bottom_left.y, top_right.y)) + ceil(scale_factor[1]));

	if (!input0()->deepEngine(myBox, channels, inPlane))
		return false;

    outPlane = DeepOutputPlane(channels, box);

	for (Box::iterator it = box.begin(); it != box.end(); it++)
	{	
		int x = it.x;
		int y = it.y;

		Vector2 center(x, y);
		bottom_left.set(x - 1, y - 1);
		top_right.set(x + 1, y + 1);

		center = matrix.transform(center);
		bottom_left = matrix.transform(bottom_left);
		top_right = matrix.transform(top_right);

		std::vector<DeepPixel> inPixels;
		int amount;
		
		if (_resize_type == none)
		{
			amount = 1;
		}

		else 
		{
			amount = (ceil(std::max(bottom_left.x, top_right.x)) - floor(std::min(bottom_left.x, top_right.x)) + 1) * (ceil(std::max(bottom_left.y, top_right.y)) - floor(std::min(bottom_left.y, top_right.y)) + 1);
		}
		
		float* weight = new float[amount];


		if (_resize_type == none)
		{
			inPixels.push_back(inPlane.getPixel((int)center.y, (int)center.x));
			weight[0] = 1;
		}

		else
		{
			int counter = 0;
			float weight_sum = 0;

			for (int i = floor(std::min(bottom_left.x, top_right.x)); i <= ceil(std::max(bottom_left.x, top_right.x)); i++)
			{
				for (int j = floor(std::min(bottom_left.y, top_right.y)); j <= ceil(std::max(bottom_left.y, top_right.y)); j++)
				{
					inPixels.push_back(inPlane.getPixel(j, i));

					float x_weight;
					float y_weight;

					float x_dist = abs(center.x - i) / std::max(scale_factor[0], 1.0f);
					float y_dist = abs(center.y - j) / std::max(scale_factor[1], 1.0f);

					if (x_dist < 1)
						x_weight = 2 * pow(x_dist, 3) - 3 * pow(x_dist, 2) + 1;		//cubic interpolation: 2|x|³ - 3|x|² + 1
					else
						x_weight = 0;

					if (y_dist < 1)
						y_weight = 2 * pow(y_dist, 3) - 3 * pow(y_dist, 2) + 1;
					else
						y_weight = 0;

					weight[counter] = x_weight * y_weight;
					weight_sum += weight[counter];

					counter++;
				}
			}

			//normalize weights, so their sum equals 1
			if (weight_sum > 0)
			{
				weight_sum = 1 / weight_sum;
				for (int i = 0; i < amount; i++)
					weight[i] *= weight_sum;
			}
		}


		DeepOutPixel outPixel;
		outPixel.clear();
		combineDeepPixels(inPixels, outPixel, channels, amount, weight, _drop_hidden, _drop_transparent, _threshold);
		outPlane.addPixel(outPixel);
	}
	
	return true;
}


static Op* build(Node* node) {return new msDeepReformat(node);}
const Op::Description msDeepReformat::d("msDeepReformat", 0, build);