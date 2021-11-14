/**
msDeepFunctions v1.0.0 (c) by Mark Spindler

msDeepFunctions is licensed under a Creative Commons Attribution 3.0 Unported License.

To view a copy of this license, visit http://creativecommons.org/licenses/by/3.0/.
**/



#include <numeric>
#include <algorithm>
#include <limits>
#include <cmath>
#include <cassert>
#include "DDImage/DeepOp.h"
#include "DDImage/DeepSample.h"
#include "DDImage/Filter.h"
#include "DDImage/Iop.h"
#include "DDImage/Knobs.h"
#include "DDImage/Pixel.h"
#include "DDImage/RequestData.h"



using namespace DD::Image;



void combineDeepPixels(std::vector<DeepPixel>& inPixels, DeepOutPixel& outPixel, const ChannelSet& channels, int amount, float weight[], bool drop_hidden = true, bool drop_transparent = true, float transparency_threshold = 0.0f)
{
	size_t* sampleCount = new size_t[amount];
	size_t* sampleNo = new size_t[amount];
	float* distance = new float[amount];
	float* alpha = new float[amount];
	float* alpha_accum = new float[amount];
	float alpha_accum_combined = 0;
	float designated_alpha_accum = 0;

	for (int i = 0; i < amount; i++)
	{
		sampleCount[i] = inPixels[i].getSampleCount();
		sampleNo[i] = 0;

		if (sampleCount[i] > 0)
			distance[i] = inPixels[i].getOrderedSample(sampleCount[i] - 1, Chan_DeepFront);							//get depth of closest sample from each pixel
		else
			distance[i] = FLT_MAX;

		alpha[i] = 0;
		alpha_accum[i] = 0;
	}


	while (std::accumulate(sampleNo, sampleNo + amount, 0) < std::accumulate(sampleCount, sampleCount + amount, 0))
	{	
		float* smallest_element = std::min_element(distance, distance + amount);									//get a pointer to the smallest element in the "distance" list
		int a = std::distance(distance, smallest_element);															//get the pointer's position in the list

		int inverted_sampleCount = sampleCount[a] - 1 - sampleNo[a];												//accessing sample from highest to lowest index, i.e. from closest to furthest Z distance
		alpha[a] = inPixels[a].getOrderedSample(inverted_sampleCount, Chan_Alpha);									//unaltered alpha of this sample

		if (!((alpha[a] <= transparency_threshold) && (drop_transparent == true)))									//skip transparent sample if eligable
		{
			if (alpha[a] == 0)																						//if the sample is completely transparent, it can be simply piped through 
			{
				outPixel.reserveMore(channels.size());
				foreach (z, channels)
					if (inPixels[a].channels().contains(z))
						outPixel.push_back(inPixels[a].getOrderedSample(inverted_sampleCount, z));
					else
						outPixel.push_back(0);
			}

			else
			{
				designated_alpha_accum -= alpha_accum[a] * weight[a];												//subtract a's prior contribution to the designated accumulated alpha, so it can be properly re-calculated for the current sample's depth
				alpha_accum[a] += alpha[a] * (1 - alpha_accum[a]);													//unaltered accumulated alpha up to the current depth (= from camera to the current sample's depth) in pixel a
				designated_alpha_accum += alpha_accum[a] * weight[a];												//new accumulated alpha is supposed to be the avarage of the accumulated alphas of all pixels up to the current depht										
			
				float new_alpha;
				if (designated_alpha_accum < 1)
					new_alpha = (designated_alpha_accum - alpha_accum_combined) / (1 - alpha_accum_combined);		//new alpha of the current sample needs to raise accumulated alpha of the combined pixel to it's designated value
				else
					new_alpha = alpha[a];

				alpha_accum_combined += new_alpha * (1 - alpha_accum_combined);										//add the current sample to the accumulated alpha of the combined pixel


				if (!((new_alpha <= transparency_threshold) && (drop_transparent == true)))
				{
					outPixel.reserveMore(channels.size());
					float new_alpha_factor = new_alpha / alpha[a];

					foreach (z, channels)
					{
						if ((z == Chan_DeepFront) || (z == Chan_DeepBack))
							outPixel.push_back(inPixels[a].getOrderedSample(inverted_sampleCount, z));
						else
							if (inPixels[a].channels().contains(z))
								outPixel.push_back(inPixels[a].getOrderedSample(inverted_sampleCount, z) * new_alpha_factor);	
							else
								outPixel.push_back(0);			
					}

					if ((new_alpha == 1) && (drop_hidden == true))													//end function if sample is opaque and hidden samples should be dropped
					{
						delete[] sampleCount;
						delete[] sampleNo;
						delete[] distance;
						delete[] alpha;
						delete[] alpha_accum;

						return;
					}
				}
			}
		}

		sampleNo[a]++;

		//change distance[a] to depth of next sample in a, so that one will be taken into account in the next cycle of the while loop
		if (sampleNo[a] < sampleCount[a])
			distance[a] = inPixels[a].getOrderedSample(inverted_sampleCount - 1, Chan_DeepFront);
		else
			distance[a] = FLT_MAX;
	}

	delete[] sampleCount;
	delete[] sampleNo;
	delete[] distance;
	delete[] alpha;
	delete[] alpha_accum;
}


void makeDeepPixelTidy(DeepPixel& inPixel, DeepOutPixel& outPixel, const ChannelSet& channels)
{
	//create sorted list of all sample distances (only front for flat samples, front and back for volumetric samples)
	std::vector<float> distance_list;

	for (int i = 0; i < inPixel.getSampleCount(); i++)
	{
		distance_list.push_back(inPixel.getOrderedSample(i, Chan_DeepFront));
		if (inPixel.getOrderedSample(i, Chan_DeepBack) != inPixel.getOrderedSample(i, Chan_DeepFront))
			distance_list.push_back(inPixel.getOrderedSample(i, Chan_DeepBack));
	}

	std::sort(distance_list.begin(), distance_list.end());

	DeepOutPixel tempPixel_splitting;
	tempPixel_splitting.clear();

	int sampleNo = 0;
	bool split = false;
	ChannelMap myChannelMap(channels);
	DeepSample tempSample_splitting(myChannelMap);
	float front;
	float back;
	int last_reached_entry = 0;

	//check all volumetric samples for overlapping and split them if necessary
	while ((sampleNo < inPixel.getSampleCount()) && (split = false))
	{
		if (split == false)
		{
			front = inPixel.getOrderedSample(sampleNo, Chan_DeepFront);
			back = inPixel.getOrderedSample(sampleNo, Chan_DeepBack);
		}
	}
	
	DeepOutPixel tempPixel_merging;
	tempPixel_merging.clear();
}