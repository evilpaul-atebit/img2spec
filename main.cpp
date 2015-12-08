/*
 *	Copyright (c) 2015 Jari Komppa, http://iki.fi/sol
 *
 *	This software is provided 'as-is', without any express or implied
 *	warranty. In no event will the authors be held liable for any damages
 *	arising from the use of this software.
 *
 *	Permission is granted to anyone to use this software for any purpose,
 *	including commercial applications, and to alter it and redistribute it
 *	freely, subject to the following restrictions:
 *
 *	1. The origin of this software must not be misrepresented; you must not
 *	claim that you wrote the original software. If you use this software
 *	in a product, an acknowledgement in the product documentation would be
 *	appreciated but is not required.
 *	2. Altered source versions must be plainly marked as such, and must not be
 *	misrepresented as being the original software.
 *	3. This notice may not be removed or altered from any source distribution.
 */

/*
Note that this is (mostly) one evening hack, so the
code quality leaves a lot to be desired..
Still, if you find it useful, great!
*/

#define _CRT_SECURE_NO_WARNINGS
#include <Windows.h>
#include "imgui.h"
#include "imgui_impl_sdl.h"
#include <stdio.h>
#include <SDL.h>
#include <SDL_opengl.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#define VERSION "1.2"

#define SPEC_Y(y)  ((((y>>0)&7)<< 3) | (((y >> 3)&7) <<0) | ((y >> 6) & 3) << 6)

int gSpeccyPalette[]
{
	0x000000, // black
	0xc00000, // blue
	0x0000c0, // red
	0xc000c0, // purple
	0x00c000, // green
	0xc0c000, // cyan
	0x00c0c0, // yellow
	0xc0c0c0, // white

	0x000000, // bright
	0xff0000,
	0x0000ff,
	0xff00ff,
	0x00ff00,
	0xffff00,
	0x00ffff,
	0xffffff
};

int rgb_to_speccy_pal(int c, int first, int count);
int float_to_color(float aR, float aG, float aB);


// used to avoid id collisions in ImGui
int gUniqueValueCounter = 0;

bool gWindowZoomedOutput = false;
bool gWindowAttribBitmap = false;
bool gWindowHistograms = false;
bool gWindowOptions = false;
bool gWindowModifierPalette = false;
bool gWindowAbout = false;
bool gWindowHelp = false;
int gOptZoom = 2;
int gOptZoomStyle = 0;
int gOptAttribOrder = 0;
int gOptBright = 2;
int gOptPaper = 0;
int gOptCellSize = 0;
int gOptScreenOrder = 1;

// Texture handles
GLuint gTextureOrig, gTextureProc, gTextureSpec, gTextureAttr, gTextureBitm; 

// Source image
unsigned int* gSourceImageData = NULL;
unsigned int gSourceImageWidth = 0;
unsigned int gSourceImageHeight = 0;

// Bitmaps for the textures
unsigned int gBitmapOrig[256 * 192];
unsigned int gBitmapProc[256 * 192];
unsigned int gBitmapSpec[256 * 192];
unsigned int gBitmapAttr[256 * 192];
unsigned int gBitmapBitm[256 * 192];

// Floating point version of the processed bitmap, for processing
float gBitmapProcFloat[256 * 192 * 3];

// Spectrum format data
unsigned char gSpectrumAttributes[32 * 24 * 8]; // big enough for 8x1 attribs
unsigned char gSpectrumBitmap[32 * 192];

// Histogram arrays
float gHistogramR[256];
float gHistogramG[256];
float gHistogramB[256];


class Modifier;
// Modifier stack
Modifier *gModifierRoot = 0;
// Modifiers are applied in reverse order
Modifier *gModifierApplyStack = 0;

class Modifier
{
public:
	int mUnique;
	bool mEnabled;
	Modifier * mNext;
	Modifier * mApplyNext;

	Modifier()
	{ 
		gUniqueValueCounter++; 
		mUnique = gUniqueValueCounter; 
		mEnabled = true; 
	}

	int common()
	{
		int ret = 0;
		ImGui::Checkbox("Enabled", &mEnabled);
		ImGui::SameLine();
		if (ImGui::Button("Remove")) ret = -1;
		ImGui::SameLine();
		if (ImGui::Button("Move down")) ret = 1;
		return ret;
	}

	virtual int ui() = 0;	
	virtual void process() = 0;
};

class RGBModifier : public Modifier
{
public:
	float mR, mG, mB;
	int mOnce;

	RGBModifier() 
	{ 
		mR = mG = mB = 0; 
		mOnce = 0;
	}

	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);

		if (!mOnce)
		{
			ImGui::OpenNextNode(1);
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("RGB Modifier"))
		{
			ret = common();
			ImGui::SliderFloat("##Red  ", &mR, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##red   ")) mR = 0; ImGui::SameLine();
			ImGui::Text("Red");

			ImGui::SliderFloat("##Green", &mG, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##green ")) mG = 0; ImGui::SameLine();
			ImGui::Text("Green");

			ImGui::SliderFloat("##Blue ", &mB, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##blue  ")) mB = 0; ImGui::SameLine();
			ImGui::Text("Blue");
		}
		ImGui::PopID();
		return ret;
	}
	
	virtual void process()
	{
		int i;
		for (i = 0; i < 256 * 192; i++)
		{
			gBitmapProcFloat[i * 3 + 0] += mB;
			gBitmapProcFloat[i * 3 + 1] += mG;
			gBitmapProcFloat[i * 3 + 2] += mR;
		}
	}
	
};

class YIQModifier : public Modifier
{
public:
	float mY, mI, mQ;
	int mOnce;

	YIQModifier()
	{
		mY = mI = mQ = 0;
		mOnce = 0;
	}

	virtual int ui()
	{
		
		int ret = 0;
		
		ImGui::PushID(mUnique);

		if (!mOnce)
		{
			ImGui::OpenNextNode(1);
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("YIQ Modifier"))
		{
			ret = common();

			ImGui::SliderFloat("##Y  ", &mY, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##Y   ")) mY = 0; ImGui::SameLine();
			ImGui::Text("Y (brightness)");

			ImGui::SliderFloat("##I", &mI, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##I ")) mI = 0; ImGui::SameLine();
			ImGui::Text("I (blue-red)");

			ImGui::SliderFloat("##Q ", &mQ, -1, 1); ImGui::SameLine();
			if (ImGui::Button("Reset##Q  ")) mQ = 0; ImGui::SameLine();
			ImGui::Text("Q (green-purple)");
		}
		ImGui::PopID();
		
		return ret;
	}

	virtual void process()
	{
		int c;
		for (c = 0; c < 256 * 192; c++)
		{
			float r = gBitmapProcFloat[c * 3 + 2];
			float g = gBitmapProcFloat[c * 3 + 1];
			float b = gBitmapProcFloat[c * 3 + 0];
				
			float y = 0.299f * r + 0.587f * g + 0.114f * b;
			float i = 0.596f * r - 0.274f * g - 0.322f * b;
			float q = 0.211f * r - 0.523f * g + 0.312f * b;
				
			y += mY;
			i += mI;
			q += mQ;
				
			r = y + 0.956f * i + 0.621f * q;
			g = y - 0.272f * i - 0.647f * q;
			b = y - 1.106f * i + 1.703f * q;

			gBitmapProcFloat[c * 3 + 0] = b;
			gBitmapProcFloat[c * 3 + 1] = g;
			gBitmapProcFloat[c * 3 + 2] = r;
		}
	}
};


class NoiseModifier : public Modifier
{
public:
	float mV;
	int mSeed;
	bool mColornoise;
	bool mR_en, mG_en, mB_en;
	int mOnce;

	NoiseModifier()
	{
		mV = 0; 
		mSeed = 0; 
		mColornoise = false; 
		mOnce = 0;
		mR_en = true;
		mG_en = true;
		mB_en = true;
	}
	
	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);
	
		if (!mOnce) 
		{
			ImGui::OpenNextNode(1); 
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("Noise Modifier"))
		{
			ret = common();			
			ImGui::SliderFloat("##Strength", &mV, 0, 0.25f);	ImGui::SameLine(); 
			if (ImGui::Button("Reset##strength   ")) mV = 0; ImGui::SameLine();
			if (ImGui::Button("Randomize##strength")) mV = 0.25f * ((SDL_GetTicks() * 74531) % 65535)/65535.0f; ImGui::SameLine();
			ImGui::Text("Strength");

			ImGui::SliderInt("##Seed    ", &mSeed, 0, 65535); ImGui::SameLine(); 
			if (ImGui::Button("Reset##seed       ")) mSeed = 0; ImGui::SameLine();  
			if (ImGui::Button("Randomize")) mSeed = (SDL_GetTicks() * 74531) % 65535; ImGui::SameLine();
			ImGui::Text("Seed");

			ImGui::Checkbox("Color noise", &mColornoise);   ImGui::SameLine(); 
			ImGui::Checkbox("Red enable", &mR_en); ImGui::SameLine(); 
			ImGui::Checkbox("Green enable", &mG_en); ImGui::SameLine(); 
			ImGui::Checkbox("Blue enable", &mB_en);
		}
		ImGui::PopID();
		return ret;
	}

	virtual void process()
	{

		int i;
		srand(mSeed);
		for (i = 0; i < 256 * 192; i++)
		{

			float r = (((rand() % 1024) - 512) / 512.0f) * mV;
			float g = (((rand() % 1024) - 512) / 512.0f) * mV;
			float b = (((rand() % 1024) - 512) / 512.0f) * mV;
			if (!mColornoise)
			{
				r = g = b;
			}
			if (mB_en) gBitmapProcFloat[i * 3 + 0] += b;
			if (mG_en) gBitmapProcFloat[i * 3 + 1] += g;
			if (mR_en) gBitmapProcFloat[i * 3 + 2] += r;
		}
	}
};

class OrderedDitherModifier : public Modifier
{
public:
	float mV;
	bool mR_en, mG_en, mB_en;
	int mOnce;
	int mXOfs, mYOfs;
	int mMatrix;

	OrderedDitherModifier()
	{
		mV = 1.0f;
		mOnce = 0;
		mR_en = true;
		mG_en = true;
		mB_en = true;
		mXOfs = 0;
		mYOfs = 0;
		mMatrix = 4;
	}

	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);

		if (!mOnce)
		{
			ImGui::OpenNextNode(1);
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("Ordered Dither Modifier"))
		{
			ret = common();
			ImGui::Combo(    "##Matrix  ", &mMatrix, "2x2\0" "3x3\0" "3x3 (alt)\0" "4x4\0" "8x8\0"); ImGui::SameLine();
			if (ImGui::Button("Reset##matrix     ")) mMatrix = 4; ImGui::SameLine();
			ImGui::Text("Matrix");

			ImGui::SliderInt("##X offset", &mXOfs, 0, 8);	ImGui::SameLine();
			if (ImGui::Button("Reset##X offset   ")) mXOfs = 0; ImGui::SameLine();
			ImGui::Text("X Offset");

			ImGui::SliderInt("##Y offset", &mYOfs, 0, 8);	ImGui::SameLine();
			if (ImGui::Button("Reset##Y offset   ")) mYOfs = 0; ImGui::SameLine();
			ImGui::Text("Y Offset");

			ImGui::SliderFloat("##Strength", &mV, 0, 2);	ImGui::SameLine();
			if (ImGui::Button("Reset##strength   ")) mV = 1.0f; ImGui::SameLine();
			ImGui::Text("Strength");

			ImGui::Checkbox("Red enable", &mR_en); ImGui::SameLine();
			ImGui::Checkbox("Green enable", &mG_en); ImGui::SameLine();
			ImGui::Checkbox("Blue enable", &mB_en);
		}
		ImGui::PopID();
		return ret;
	}

	virtual void process()
	{

		float matrix2x2[] =
		{
			1.0f,  3.0f,
			4.0f,  2.0f
		};

		float matrix3x3[] =
		{
			8.0f,  3.0f,  4.0f,
			6.0f,  1.0f,  2.0f,
			7.0f,  5.0f,  9.0f
		};

		float matrix3x3alt[] =
		{
			1.0f,  7.0f,  4.0f,
			5.0f,  8.0f,  3.0f,
			6.0f,  2.0f,  9.0f
		};

		float matrix4x4[] =
		{
			1.0f,  9.0f,  3.0f, 11.0f,
			13.0f,  5.0f, 15.0f,  7.0f,
			4.0f, 12.0f,  2.0f, 10.0f,
			16.0f,  8.0f, 14.0f,  6.0f
		};

		float matrix8x8[] =
		{
			0.0f, 32.0f, 8.0f, 40.0f, 2.0f, 34.0f, 10.0f, 42.0f,
			48.0f, 16.0f, 56.0f, 24.0f, 50.0f, 18.0f, 58.0f, 26.0f,
			12.0f, 44.0f, 4.0f, 36.0f, 14.0f, 46.0f, 6.0f, 38.0f,
			60.0f, 28.0f, 52.0f, 20.0f, 62.0f, 30.0f, 54.0f, 22.0f,
			3.0f, 35.0f, 11.0f, 43.0f, 1.0f, 33.0f, 9.0f, 41.0f,
			51.0f, 19.0f, 59.0f, 27.0f, 49.0f, 17.0f, 57.0f, 25.0f,
			15.0f, 47.0f, 7.0f, 39.0f, 13.0f, 45.0f, 5.0f, 37.0f,
			63.0f, 31.0f, 55.0f, 23.0f, 61.0f, 29.0f, 53.0f, 21.0f
		};

		float *matrix;
		int matsize;
		float matdiv;

		switch (mMatrix)
		{
		case 0:
			matrix = matrix2x2;
			matsize = 2;
			matdiv = 4.0f;
			break;
		case 1:
			matrix = matrix3x3;
			matsize = 3;
			matdiv = 9.0f;
			break;
		case 2:
			matrix = matrix3x3alt;
			matsize = 3;
			matdiv = 9.0f;
			break;
		case 3:
			matrix = matrix4x4;
			matsize = 4;
			matdiv = 16.0f;
			break;
		case 4:
			matrix = matrix8x8;
			matsize = 8;
			matdiv = 63.0f;
			break;
		}
		int i, j;
		for (i = 0; i < 192; i++)
		{
			for (j = 0; j < 256; j++)
			{
				if (mB_en) gBitmapProcFloat[(i * 256 + j) * 3 + 0] += (matrix[((i + mYOfs) % matsize) * matsize + ((j + mXOfs) % matsize)] / matdiv - 0.5f) * gBitmapProcFloat[(i * 256 + j) * 3 + 0] * mV;
				if (mG_en) gBitmapProcFloat[(i * 256 + j) * 3 + 1] += (matrix[((i + mYOfs) % matsize) * matsize + ((j + mXOfs) % matsize)] / matdiv - 0.5f) * gBitmapProcFloat[(i * 256 + j) * 3 + 1] * mV;
				if (mR_en) gBitmapProcFloat[(i * 256 + j) * 3 + 2] += (matrix[((i + mYOfs) % matsize) * matsize + ((j + mXOfs) % matsize)] / matdiv - 0.5f) * gBitmapProcFloat[(i * 256 + j) * 3 + 2] * mV;
			}
		}
	}
};

class ErrorDiffusionDitherModifier : public Modifier
{
public:
	bool mR_en, mG_en, mB_en;
	float mV;
	int mModel;
	int mDirection;
	int mOnce;

	ErrorDiffusionDitherModifier()
	{
		mV = 1;
		mModel = 0;
		mOnce = 0;
		mDirection = 0;
	}

	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);

		if (!mOnce)
		{
			ImGui::OpenNextNode(1);
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("Error Diffusion Dither Modifier"))
		{
			ret = common();

			ImGui::Combo("##Model  ", &mModel, "Floyd-Steinberg\0Jarvis-Judice-Ninke\0Stucki\0Burkes\0Sierra3\0Sierra2\0Sierra2-4A\0"); ImGui::SameLine();
			if (ImGui::Button("Reset##model     ")) mModel = 0; ImGui::SameLine();
			ImGui::Text("Model");

			ImGui::Combo("##Direction  ", &mDirection, "Left-right\0Right-left\0Bidirectional, left-right first\0Bidirectional, right-left first\0"); ImGui::SameLine();
			if (ImGui::Button("Reset##Direction     ")) mDirection = 0; ImGui::SameLine();
			ImGui::Text("Direction");

			ImGui::SliderFloat("##Strength", &mV, 0, 2);	ImGui::SameLine();
			if (ImGui::Button("Reset##strength   ")) mV = 1.0f; ImGui::SameLine();
			ImGui::Text("Strength");

			ImGui::Checkbox("Red enable", &mR_en); ImGui::SameLine();
			ImGui::Checkbox("Green enable", &mG_en); ImGui::SameLine();
			ImGui::Checkbox("Blue enable", &mB_en);
		}
		ImGui::PopID();
		return ret;
	}

	virtual void process()
	{
		float data[256 * 192 * 3];
		memcpy(data, gBitmapProcFloat, sizeof(float) * 256 * 192 * 3);
		int i, j;

		float floyd_steinberg[] =
		{
			0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 7.0f / 16.0f, 0.0f / 16.0f,
			0.0f / 16.0f, 3.0f / 16.0f, 5.0f / 16.0f, 1.0f / 16.0f, 0.0f / 16.0f,
			0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f
		};
		
		float jarvis_judice_ninke[] =
		{
			0.0f / 48.0f, 0.0f / 48.0f, 0.0f / 48.0f, 7.0f / 48.0f, 5.0f / 48.0f,
			3.0f / 48.0f, 5.0f / 48.0f, 7.0f / 48.0f, 5.0f / 48.0f, 3.0f / 48.0f,
			1.0f / 48.0f, 3.0f / 48.0f, 5.0f / 48.0f, 3.0f / 48.0f, 1.0f / 48.0f
		};

		float stucki[] =
		{
			0.0f / 42.0f, 0.0f / 42.0f, 0.0f / 42.0f, 8.0f / 42.0f, 4.0f / 42.0f,
			2.0f / 42.0f, 4.0f / 42.0f, 8.0f / 42.0f, 4.0f / 42.0f, 2.0f / 42.0f,
			1.0f / 42.0f, 2.0f / 42.0f, 4.0f / 42.0f, 2.0f / 42.0f, 1.0f / 42.0f
		};

		float burkes[] =
		{
			0.0f / 32.0f, 0.0f / 32.0f, 0.0f / 32.0f, 8.0f / 32.0f, 4.0f / 32.0f,
			2.0f / 32.0f, 4.0f / 32.0f, 8.0f / 32.0f, 4.0f / 32.0f, 2.0f / 32.0f,
			0.0f / 32.0f, 0.0f / 32.0f, 0.0f / 32.0f, 0.0f / 32.0f, 0.0f / 32.0f
		};

		float sierra3[] =
		{
			0.0f / 32.0f, 0.0f / 32.0f, 0.0f / 32.0f, 5.0f / 32.0f, 3.0f / 32.0f,
			2.0f / 32.0f, 4.0f / 32.0f, 5.0f / 32.0f, 4.0f / 32.0f, 2.0f / 32.0f,
			0.0f / 32.0f, 2.0f / 32.0f, 3.0f / 32.0f, 2.0f / 32.0f, 0.0f / 32.0f
		};

		float sierra2[] =
		{
			0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 4.0f / 16.0f, 3.0f / 16.0f,
			1.0f / 16.0f, 2.0f / 16.0f, 3.0f / 16.0f, 2.0f / 16.0f, 1.0f / 16.0f,
			0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f, 0.0f / 16.0f
		};

		float sierra2_4a[] =
		{
			0.0f / 4.0f, 0.0f / 4.0f, 0.0f / 4.0f, 2.0f / 4.0f, 0.0f / 4.0f,
			0.0f / 4.0f, 1.0f / 4.0f, 1.0f / 4.0f, 0.0f / 4.0f, 0.0f / 4.0f,
			0.0f / 4.0f, 0.0f / 4.0f, 0.0f / 4.0f, 0.0f / 4.0f, 0.0f / 4.0f
		};
		
		float *matrix = floyd_steinberg;

		switch (mModel)
		{
		case 0:
			matrix = floyd_steinberg;
			break;
		case 1:
			matrix = jarvis_judice_ninke;
			break;
		case 2:
			matrix = stucki;
			break;
		case 3:
			matrix = burkes;
			break;
		case 4:
			matrix = sierra3;
			break;
		case 5:
			matrix = sierra2;
			break;
		case 6:
			matrix = sierra2_4a;
			break;
		}

		int xpos, ypos;
		int xpos0 = 0, ypos0 = 0;
		int xposi = 1, yposi = 1;
		int dir = 0;
		switch (mDirection)
		{
		case 0:
		case 2:
			xpos0 = 0;
			ypos0 = 0;
			xposi = 1;
			yposi = 1;
			dir = 0;
			break;
		case 1:
		case 3:
			xpos0 = 255;
			ypos0 = 0;
			xposi = -1;
			yposi = 1;
			dir = 1;
			break;
		}

		for (i = 0, ypos = ypos0; i < 192; i++, ypos+=yposi)
		{			
			for (j = 0, xpos = xpos0; j < 256; j++, xpos+=xposi)
			{
				int pos = (ypos * 256 + xpos);
				
				int col = float_to_color(
					data[pos * 3 + 0], 
					data[pos * 3 + 1], 
					data[pos * 3 + 2]);

				int approx = gSpeccyPalette[rgb_to_speccy_pal(col, 0, 16)];
				
				float r = data[pos * 3 + 2] - ((approx >> 0) & 0xff) / 255.0f;
				float g = data[pos * 3 + 1] - ((approx >> 8) & 0xff) / 255.0f;
				float b = data[pos * 3 + 0] - ((approx >> 16) & 0xff) / 255.0f;

				int x, y;
				for (y = 0; y < 3; y++)
				{					
					for (x = 0; x < 5; x++)
					{
						if (ypos + y < 192 && x + xpos - 2 >= 0 && x + xpos - 2 < 256)
						{
							int pos = ((ypos + y) * 256 + xpos + x - 2);
							float m;
							if (dir)
							{
								m = matrix[y * 5 + 4 - x];
							}							
							else
							{
								m = matrix[y * 5 + x];
							}
							
							data[pos * 3 + 2] += r * m;
							data[pos * 3 + 1] += g * m;
							data[pos * 3 + 0] += b * m;
						}
					}
				}

			}
			if (mDirection > 1)
			{
				switch (dir)
				{
				case 0:
					xpos0 = 255;
					ypos0 = 0;
					xposi = -1;
					yposi = 1;
					dir = 1;
					break;
				case 1:
					xpos0 = 0;
					ypos0 = 0;
					xposi = 1;
					yposi = 1;
					dir = 0;
					break;
				}
			}
		}

		// Apply (with strength)
		for (i = 0; i < 256 * 192; i++)
		{				
			if (mB_en) gBitmapProcFloat[i * 3 + 0] += (data[i * 3 + 0] - gBitmapProcFloat[i * 3 + 0]) * mV;
			if (mG_en) gBitmapProcFloat[i * 3 + 1] += (data[i * 3 + 1] - gBitmapProcFloat[i * 3 + 1]) * mV;
			if (mR_en) gBitmapProcFloat[i * 3 + 2] += (data[i * 3 + 2] - gBitmapProcFloat[i * 3 + 2]) * mV;
		}
	}

};


class ContrastModifier : public Modifier
{
public:
	float mC, mB, mP;
	int mOnce;

	ContrastModifier()
	{
		mB = 0;
		mC = 1;
		mP = 0.5f;
		mOnce = 0;
	}

	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);
		
		if (!mOnce) {
			ImGui::OpenNextNode(1); 
			mOnce = 1;
		}

		if (ImGui::CollapsingHeader("Contrast Modifier"))
		{
			ret = common();
			
			ImGui::SliderFloat("##Contrast  ", &mC, 0, 5);  ImGui::SameLine();	
			if (ImGui::Button("Reset##contrast   ")) mC = 1; ImGui::SameLine();
			ImGui::Text("Contrast");

			ImGui::SliderFloat("##Brightness", &mB, -2, 2); ImGui::SameLine();	
			if (ImGui::Button("Reset##brightness ")) mB = 0; ImGui::SameLine();
			ImGui::Text("Brightness");

			ImGui::SliderFloat("##Pivot     ", &mP, -2, 2); ImGui::SameLine();
			if (ImGui::Button("Reset##pivot      ")) mB = 0.5f; ImGui::SameLine();
			ImGui::Text("Pivot");
		}
		ImGui::PopID();
		return ret;
	}

	virtual void process()
	{

		int i;
		for (i = 0; i < 256 * 192 * 3; i++)
		{
			gBitmapProcFloat[i] = (gBitmapProcFloat[i] - mP) * mC + mP + mB;
		}
	}
};


class HSVModifier : public Modifier
{
public:
	float mH, mS, mV;
	int mOnce;
	
	HSVModifier()
	{
		mH = 0;
		mS = 0;
		mV = 0;
		mOnce = 0;
	}

	void rgb2hsv(float r, float g, float b, float &h, float &s, float &v)
	{
		float      min, max, delta;

		min = r < g ? r : g;
		min = min  < b ? min : b;

		max = r > g ? r : g;
		max = max > b ? max : b;

		v = max;                                // v
		delta = max - min;
		if (delta < 0.00001)
		{
			s = 0;
			h = 0; // undefined, maybe nan?
			return;
		}
		if (max > 0.0) { // NOTE: if Max is == 0, this divide would cause a crash
			s = (delta / max);                  // s
		}
		else {
			// if max is 0, then r = g = b = 0              
			// s = 0, v is undefined
			s = 0.0;
			h = 0.0;                            // its now undefined
			return;
		}
		if (r >= max)                           // > is bogus, just keeps compilor happy
			h = (g - b) / delta;        // between yellow & magenta
		else
			if (g >= max)
				h = 2.0f + (b - r) / delta;  // between cyan & yellow
			else
				h = 4.0f + (r - g) / delta;  // between magenta & cyan

		h *= 60.0f;                              // degrees

		if (h < 0.0f)
			h += 360.0f;
	}


	void hsv2rgb(float h, float s, float v, float &r, float &g, float &b)
	{
		float      hh, p, q, t, ff;
		long        i;

		if (s <= 0.0f) {       // < is bogus, just shuts up warnings
			r = v;
			g = v;
			b = v;
			return;
		}
		hh = h;
		if (hh >= 360.0f) hh = 0.0f;
		hh /= 60.0f;
		i = (long)floor(hh);
		ff = hh - i;
		p = v * (1.0f - s);
		q = v * (1.0f - (s * ff));
		t = v * (1.0f - (s * (1.0f - ff)));

		switch (i) {
		case 0:
			r = v;
			g = t;
			b = p;
			break;
		case 1:
			r = q;
			g = v;
			b = p;
			break;
		case 2:
			r = p;
			g = v;
			b = t;
			break;

		case 3:
			r = p;
			g = q;
			b = v;
			break;
		case 4:
			r = t;
			g = p;
			b = v;
			break;
		case 5:
		default:
			r = v;
			g = p;
			b = q;
			break;
		}
	}

	virtual int ui()
	{
		int ret = 0;
		ImGui::PushID(mUnique);

		if (!mOnce) 
		{
			ImGui::OpenNextNode(1); 
			mOnce = 1;
		}
		
		if (ImGui::CollapsingHeader("HSV Modifier"))
		{
			ret = common();

			ImGui::SliderFloat("##Hue       ", &mH, -360, 360); ImGui::SameLine();	
			if (ImGui::Button("Reset##hue        ")) mH = 0; ImGui::SameLine();
			ImGui::Text("Hue (color)");
			
			ImGui::SliderFloat("##Saturation", &mS, -2, 2);     ImGui::SameLine();	
			if (ImGui::Button("Reset##saturation ")) mS = 0; ImGui::SameLine();
			ImGui::Text("Saturation (richness)");
			
			ImGui::SliderFloat("##Value     ", &mV, -2, 2);     ImGui::SameLine();	
			if (ImGui::Button("Reset##value      ")) mV = 0; ImGui::SameLine();
			ImGui::Text("Value (brightness)");
		}
		ImGui::PopID();
		return ret;
	}

	virtual void process()
	{

		int i;
		for (i = 0; i < 256 * 192; i++)
		{
			rgb2hsv(gBitmapProcFloat[i * 3 + 0], gBitmapProcFloat[i * 3 + 1], gBitmapProcFloat[i * 3 + 2], gBitmapProcFloat[i * 3 + 0], gBitmapProcFloat[i * 3 + 1], gBitmapProcFloat[i * 3 + 2]);
				
			gBitmapProcFloat[i * 3 + 0] += mH;
			gBitmapProcFloat[i * 3 + 1] += mS;
			gBitmapProcFloat[i * 3 + 2] += mV;
				
			if (gBitmapProcFloat[i * 3 + 0] < 0) gBitmapProcFloat[i * 3 + 0] += 360;
			if (gBitmapProcFloat[i * 3 + 0] > 360) gBitmapProcFloat[i * 3 + 0] -= 360;
				
			hsv2rgb(gBitmapProcFloat[i * 3 + 0], gBitmapProcFloat[i * 3 + 1], gBitmapProcFloat[i * 3 + 2], gBitmapProcFloat[i * 3 + 0], gBitmapProcFloat[i * 3 + 1], gBitmapProcFloat[i * 3 + 2]);
		}
	}
};

void bitmap_to_float(unsigned int *aBitmap)
{
	int i;
	for (i = 0; i < 256 * 192; i++)
	{
		int c = aBitmap[i];
		int r = (c >> 16) & 0xff;
		int g = (c >> 8) & 0xff;
		int b = (c >> 0) & 0xff;

		gBitmapProcFloat[i * 3 + 0] = r * (1.0f / 255.0f);
		gBitmapProcFloat[i * 3 + 1] = g * (1.0f / 255.0f);
		gBitmapProcFloat[i * 3 + 2] = b * (1.0f / 255.0f);
	}
}

int float_to_color(float aR, float aG, float aB)
{
	aR = (aR < 0) ? 0 : (aR > 1) ? 1 : aR;
	aG = (aG < 0) ? 0 : (aG > 1) ? 1 : aG;
	aB = (aB < 0) ? 0 : (aB > 1) ? 1 : aB;

	return ((int)floor(aR * 255) << 16) |
		   ((int)floor(aG * 255) << 8) |
		   ((int)floor(aB * 255) << 0);
}

void float_to_bitmap()
{
	int i;
	for (i = 0; i < 256 * 192; i++)
	{
		float r = gBitmapProcFloat[i * 3 + 0];
		float g = gBitmapProcFloat[i * 3 + 1];
		float b = gBitmapProcFloat[i * 3 + 2];

		gBitmapProc[i] = float_to_color(r, g, b) | 0xff000000;
	}
}

void modifier_ui()
{
	Modifier *walker = gModifierRoot;
	Modifier *prev = 0;
	
	while (walker)
	{
		int ret = walker->ui();
		if (ret == 1) // move down
		{
			if (walker->mNext)
			{
				Modifier *curr = walker;
				Modifier *next = walker->mNext;
				/*
				prev->curr->next->..
				*/
				curr->mNext = next->mNext;
				next->mNext = curr;
				if (prev == 0)
				{
					gModifierRoot = next;
				}
				else
				{
					prev->mNext = next;
				}
			}
			else
			{
				prev = walker;
				walker = walker->mNext;
			}
		}
		else
			if (ret == -1) // delete
			{
				Modifier *t = walker;
				if (prev == 0)
				{
					gModifierRoot = walker->mNext;
				}
				else
				{
					prev->mNext = walker->mNext;
				}
				walker = walker->mNext;
				delete t;
			}
			else // normal op
			{
				prev = walker;
				walker = walker->mNext;
			}
	}

	gModifierApplyStack = gModifierRoot;
	if (gModifierApplyStack)
	{
		gModifierApplyStack->mApplyNext = 0;

		walker = gModifierRoot->mNext;
		while (walker)
		{
			walker->mApplyNext = gModifierApplyStack;
			gModifierApplyStack = walker;
			walker = walker->mNext;
		}
	}
}

void process_image()
{
	bitmap_to_float(gBitmapOrig);

	Modifier *walker = gModifierApplyStack;
	while (walker)
	{
		if (walker->mEnabled)
			walker->process();
		walker = walker->mApplyNext;
	}

	float_to_bitmap();
}


int rgb_to_speccy_pal(int c, int first, int count)
{
	int i;
	int r = (c >> 16) & 0xff;
	int g = (c >> 8) & 0xff;
	int b = (c >> 0) & 0xff;
	int bestdist = 256*256*4;
	int best = 0;

	for (i = first; i < first + count; i++)
	{
		int s_b = (gSpeccyPalette[i] >> 0) & 0xff;
		int s_g = (gSpeccyPalette[i] >> 8) & 0xff;
		int s_r = (gSpeccyPalette[i] >> 16) & 0xff;

		int dist = (r - s_r) * (r - s_r) +
			(g - s_g) * (g - s_g) +
			(b - s_b) * (b - s_b);

		if (dist < bestdist)
		{
			best = i;
			bestdist = dist;
		}
	}
	return best; // gSpeccyPalette[best] | 0xff000000;
}

int pick_from_2_speccy_cols(int c, int col1, int col2)
{
	int r = (c >> 16) & 0xff;
	int g = (c >> 8) & 0xff;
	int b = (c >> 0) & 0xff;

	int s_b = (gSpeccyPalette[col1] >> 0) & 0xff;
	int s_g = (gSpeccyPalette[col1] >> 8) & 0xff;
	int s_r = (gSpeccyPalette[col1] >> 16) & 0xff;

	int dist1 = (r - s_r) * (r - s_r) +
		(g - s_g) * (g - s_g) +
		(b - s_b) * (b - s_b);

	s_b = (gSpeccyPalette[col2] >> 0) & 0xff;
	s_g = (gSpeccyPalette[col2] >> 8) & 0xff;
	s_r = (gSpeccyPalette[col2] >> 16) & 0xff;

	int dist2 = (r - s_r) * (r - s_r) +
		(g - s_g) * (g - s_g) +
		(b - s_b) * (b - s_b);

	if (dist1 < dist2)
		return col1;
	return col2;
}


void spectrumize_image()
{
	int x, y, i, j;

	// Find closest colors in the speccy palette
	for (i = 0; i < 256 * 192; i++)
	{		
		gBitmapSpec[i] = rgb_to_speccy_pal(gBitmapProc[i], 0, 16);
	}

	int ymax;
	int cellht;
	switch (gOptCellSize)
	{
	case 0:
		ymax = 24;
		cellht = 8;
		break;
	case 1:
		ymax = 48;
		cellht = 4;
		break;
	case 2:
		ymax = 96;
		cellht = 2;
		break;
	case 3:
		ymax = 192;
		cellht = 1;
		break;
	}

	for (y = 0; y < ymax; y++)
	{
		for (x = 0; x < 32; x++)
		{
			// Count bright pixels in cell
			int brights = 0;
			int blacks = 0;
			for (i = 0; i < cellht; i++)
			{
				for (j = 0; j < 8; j++)
				{
					int loc = (y * cellht + i) * 256 + x * 8 + j;
					if (gBitmapSpec[loc] > 7)
						brights++;
					if (gBitmapSpec[loc] == 0)
						blacks++;
				}
			}

			switch (gOptBright)
			{
			case 0: // only dark
				brights = 0;
				break;
			case 1: // prefer dark
				brights = (brights >= (8 * cellht - blacks)) * 8;
				break;
			case 2: // default
				brights = (brights >= (8 * cellht - blacks) / 2) * 8;
				break;
			case 3: // prefer bright
				brights = (brights >= (8 * cellht - blacks) / 4) * 8;
				break;
			case 4: // only bright
				brights = 8;
				break;
			}		

			int counts[16];
			for (i = 0; i < 16; i++)
				counts[i] = 0;

			// Remap with just bright OR dark colors, based on whether the whole cell is bright or not
			for (i = 0; i < cellht; i++)
			{
				for (j = 0; j < 8; j++)
				{
					int loc = (y * cellht + i) * 256 + x * 8 + j;
					int r = rgb_to_speccy_pal(gBitmapProc[loc], brights, 8);
					gBitmapSpec[loc] = r;
					counts[r]++;
				}
			}

			// Find two most common colors in cell
			int col1 = 0, col2 = 0;
			int best = 0;
			if (gOptPaper == 0)
			{
				for (i = 0; i < 16; i++)
				{
					if (counts[i] > best)
					{
						best = counts[i];
						col1 = i;
					}
				}
			}
			else
			{
				col1 = (gOptPaper - 1) + brights;
			}

			// reset count of selected color to zero so we don't pick it twice
			counts[col1] = 0; 
			
			best = 0;
			for (i = 0; i < 16; i++)
			{
				if (counts[i] > best)
				{
					best = counts[i];
					col2 = i;
				}
			}

			// Make sure we're using bright black
			if (col2 == 0 && col1 > 7) col2 = 8;

			// Final pass on cell, select which of two colors we can use
			for (i = 0; i < cellht; i++)
			{
				for (j = 0; j < 8; j++)
				{
					int loc = (y * cellht + i) * 256 + x * 8 + j;					
					gBitmapSpec[loc] = pick_from_2_speccy_cols(gBitmapProc[loc], col1, col2);
				}
			}		
			
			if (gOptPaper == 0)
			{
				// Make the "brighter" color the ink to make bitmap pretty (if wanted)
				if (gOptAttribOrder == 0 && col2 < col1)
				{
					int tmp = col2;
					col2 = col1;
					col1 = tmp;
				}
			}

			// Store the cell's attribute
			gSpectrumAttributes[y * 32 + x] = (col2 & 0x7) | ((col1 & 7) << 3) | (((col1 & 8) != 0) << 6);
		}
	}

	// Map color indices to palette
	for (i = 0; i < 256 * 192; i++)
	{
		gBitmapSpec[i] = gSpeccyPalette[gBitmapSpec[i]] | 0xff000000;
	}

}

void calc_histogram(unsigned int *src)
{
	int i;
	for (i = 0; i < 256; i++)
	{
		gHistogramR[i] = 0;
		gHistogramG[i] = 0;
		gHistogramB[i] = 0;
	}
	for (i = 0; i < 192 * 256; i++)
	{
		gHistogramR[(src[i] >> 0) & 0xff]++;
		gHistogramG[(src[i] >> 8) & 0xff]++;
		gHistogramB[(src[i] >> 16) & 0xff]++;
	}
}

// Should probably add to the end of the list instead of beginning..
void addModifier(Modifier *aNewModifier)
{
	aNewModifier->mNext = gModifierRoot;
	gModifierRoot = aNewModifier;
}


void copySourceToOrig()
{
    for (unsigned int x = 0; x < 256; x++)
    {
        for (unsigned int y = 0; y < 192; y++)
        {
            int pix = 0;
            if (x < gSourceImageWidth && y < gSourceImageHeight)
                pix = gSourceImageData[x + y * gSourceImageWidth] | 0xff000000;
            else
                pix = 0xffffffff;
            gBitmapOrig[x + y * 256] = pix;
        }
    }
    glBindTexture(GL_TEXTURE_2D, gTextureOrig);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)gBitmapOrig);
}


void loadImg(const char* szFileName)
{
    int width, height, n;
    unsigned int *data = (unsigned int*)stbi_load(szFileName, &width, &height, &n, 4);
    if (data)
    {
        delete[] gSourceImageData;
        gSourceImageData = new unsigned int[width * height];
        gSourceImageWidth = width;
        gSourceImageHeight = height;
        memcpy(gSourceImageData, data, width * height * sizeof(unsigned int));
        stbi_image_free(data);
    }

    copySourceToOrig();
}


void chooseLoadImg()
{
    OPENFILENAMEA ofn;
    char szFileName[1024] = "";

    ZeroMemory(&ofn, sizeof(ofn));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = 0;
    ofn.lpstrFilter =
        "All supported types\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.psd;*.gif;*.hdr;*.pic;*.pnm\0"
        "PNG (*.png)\0*.png\0"
        "JPG (*.jpg)\0*.jpg;*.jpeg\0"
        "TGA (*.tga)\0*.tga\0"
        "BMP (*.bmp)\0*.bmp\0"
        "PSD (*.psd)\0*.psd\0"
        "GIF (*.gif)\0*.gif\0"
        "HDR (*.hdr)\0*.hdr\0"
        "PIC (*.pic)\0*.pic\0"
        "PNM (*.pnm)\0*.pnm\0"
        "All Files (*.*)\0*.*\0\0";

    ofn.nMaxFile = 1024;

    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
    ofn.lpstrFile = szFileName;

    ofn.lpstrTitle = "Load image";

    if (GetOpenFileNameA(&ofn))
    {
        loadImg(szFileName);
    }
}


void savepng()
{
	OPENFILENAMEA ofn;
	char szFileName[1024] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = 0;
	ofn.lpstrFilter =
		"PNG (*.png)\0*.png\0"
		"All Files (*.*)\0*.*\0\0";

	ofn.nMaxFile = 1024;

	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrFile = szFileName;

	ofn.lpstrTitle = "Save png";

	FILE * f = NULL;

	if (GetSaveFileNameA(&ofn))
	{
		stbi_write_png(szFileName, 256, 192, 4, gBitmapProc, 256*4);
	}
}


// When converting to speccy format we process stuff cell by cell..
// easier to just analyze the result to generate the bitmap than to
// mess up the conversion further.
void grab_speccy_bitmap(int aScreenOrder)
{	
	int i, j;
	if (aScreenOrder)
	{
		for (i = 0; i < 192; i++)
		{
			for (j = 0; j < 256; j++)
			{
				gSpectrumBitmap[SPEC_Y(i) * 32 + j / 8] <<= 1;
				int v = ((gBitmapSpec[i * 256 + j] & 0xffffff) != gSpeccyPalette[((gSpectrumAttributes[(i / 8) * 32 + j / 8] >> 3) & 7) | ((gSpectrumAttributes[(i / 8) * 32 + j / 8] >> 6) << 3)]);
				gSpectrumBitmap[SPEC_Y(i) * 32 + j / 8] |= v;
			}
		}
	}
	else
	{
		for (i = 0; i < 192; i++)
		{
			for (j = 0; j < 256; j++)
			{
				gSpectrumBitmap[i * 32 + j / 8] <<= 1;
				int v = ((gBitmapSpec[i * 256 + j] & 0xffffff) != gSpeccyPalette[((gSpectrumAttributes[(i / 8) * 32 + j / 8] >> 3) & 7) | ((gSpectrumAttributes[(i / 8) * 32 + j / 8] >> 6) << 3)]);
				gSpectrumBitmap[i * 32 + j / 8] |= v;
			}
		}
	}
}

void savescr()
{
	OPENFILENAMEA ofn;
	char szFileName[1024] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = 0;
	ofn.lpstrFilter =
		"scr (*.scr)\0*.scr\0"
		"All Files (*.*)\0*.*\0\0";

	ofn.nMaxFile = 1024;

	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrFile = szFileName;

	ofn.lpstrTitle = "Save scr";

	FILE * f = NULL;

	int attrib_size_multiplier = 1 << gOptCellSize;

	if (GetSaveFileNameA(&ofn))
	{
		grab_speccy_bitmap(gOptScreenOrder);
		FILE * f = fopen(szFileName, "wb");
		fwrite(gSpectrumBitmap, 32 * 192, 1, f);
		fwrite(gSpectrumAttributes, 32 * 24 * attrib_size_multiplier, 1, f);
		fclose(f);
	}
}

void saveh()
{
	OPENFILENAMEA ofn;
	char szFileName[1024] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = 0;
	ofn.lpstrFilter =
		"h (*.h)\0*.h\0"
		"All Files (*.*)\0*.*\0\0";

	ofn.nMaxFile = 1024;

	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrFile = szFileName;

	ofn.lpstrTitle = "Save h";

	FILE * f = NULL;

	int attrib_size_multiplier = 1 << gOptCellSize;

	if (GetSaveFileNameA(&ofn))
	{
		grab_speccy_bitmap(gOptScreenOrder);
		FILE * f = fopen(szFileName, "w");
		int i, w = 0;
		for (i = 0; i < 32 * 192; i++)
		{
			w += fprintf(f, "%3d,", gSpectrumBitmap[i]);
			if (w > 75)
			{
				fprintf(f, "\n");
				w = 0;
			}				
		}
		fprintf(f,"\n\n");
		w = 0;
		for (i = 0; i < 32 * 24 * attrib_size_multiplier; i++)
		{
			w += fprintf(f, "%3d%s", gSpectrumAttributes[i], i != (32 * 24 * attrib_size_multiplier)-1?",":"");
			if (w > 75)
			{
				fprintf(f, "\n");
				w = 0;
			}
		}
		fclose(f);
	}
}


void saveinc()
{
	OPENFILENAMEA ofn;
	char szFileName[1024] = "";

	ZeroMemory(&ofn, sizeof(ofn));

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = 0;
	ofn.lpstrFilter =
		"inc (*.inc)\0*.inc\0"
		"All Files (*.*)\0*.*\0\0";

	ofn.nMaxFile = 1024;

	ofn.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY | OFN_OVERWRITEPROMPT;
	ofn.lpstrFile = szFileName;

	ofn.lpstrTitle = "Save inc";

	FILE * f = NULL;

	int attrib_size_multiplier = 1 << gOptCellSize;

	if (GetSaveFileNameA(&ofn))
	{
		grab_speccy_bitmap(gOptScreenOrder);
		FILE * f = fopen(szFileName, "w");
		int i;
		for (i = 0; i < 32 * 192; i++)
		{
			fprintf(f, "\t.db #0x%02x\n", gSpectrumBitmap[i]);
		}
		fprintf(f, "\n\n");
		for (i = 0; i < 32 * 24 * attrib_size_multiplier; i++)
		{
			fprintf(f, "\t.db #0x%02x\n", gSpectrumAttributes[i]);
		}
		fclose(f);
	}
}


void gen_attr_bitm()
{
	int cellht;
	switch (gOptCellSize)
	{
	case 0:
		cellht = 8;
		break;
	case 1:
		cellht = 4;
		break;
	case 2:
		cellht = 2;
		break;
	case 3:
		cellht = 1;
		break;
	}

	grab_speccy_bitmap(0);
	int i, j;
	for (i = 0; i < 192; i++)
	{
		for (j = 0; j < 256; j++)
		{
			int attr = gSpectrumAttributes[(i / cellht) * 32 + j / 8];
			int fg = gSpeccyPalette[((attr >> 0) & 7) | (((attr & 64)) >> 3)] | 0xff000000;
			int bg = gSpeccyPalette[((attr >> 3) & 7) | (((attr & 64)) >> 3)] | 0xff000000;
			gBitmapAttr[i * 256 + j] = (j % 8 < (((8-cellht)/2) + i % cellht)) ? fg : bg;
			gBitmapBitm[i * 256 + j] = (gSpectrumBitmap[i * 32 + j / 8] & (1 << (7-(j % 8)))) ? 0xffc0c0c0 : 0xff000000;
		}
	}
	glBindTexture(GL_TEXTURE_2D, gTextureAttr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)gBitmapAttr);
	glBindTexture(GL_TEXTURE_2D, gTextureBitm);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)gBitmapBitm);

}

int main(int argc, char** argv)
{

	// Setup SDL
	if (SDL_Init(SDL_INIT_EVERYTHING) != 0)
	{
        printf("Error: %s\n", SDL_GetError());
        return -1;
	}

    // Setup window
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
	SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
	SDL_DisplayMode current;
	SDL_GetCurrentDisplayMode(0, &current);
	SDL_Window *window = SDL_CreateWindow("Image Spectrumizer " VERSION " - http://iki.fi/sol", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
	SDL_GLContext glcontext = SDL_GL_CreateContext(window);

    // Setup ImGui binding
    ImGui_ImplSdl_Init(window);

    bool show_test_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImColor(114, 144, 154);

	glGenTextures(1, &gTextureOrig);
	glBindTexture(GL_TEXTURE_2D, gTextureOrig);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureProc);
	glBindTexture(GL_TEXTURE_2D, gTextureProc);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureSpec);
	glBindTexture(GL_TEXTURE_2D, gTextureSpec);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureAttr);
	glBindTexture(GL_TEXTURE_2D, gTextureAttr);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

	glGenTextures(1, &gTextureBitm);
	glBindTexture(GL_TEXTURE_2D, gTextureBitm);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)0);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
	glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);


    // Load an image from the command line?
    if (argc == 2)
    {
        loadImg(argv[1]);
    }


    // Main loop
	bool done = false;
    while (!done)
    {
		ImVec2 picsize(256, 192);
		
		SDL_Event event;
        while (SDL_PollEvent(&event))
        {
            ImGui_ImplSdl_ProcessEvent(&event);
            if (event.type == SDL_QUIT)
                done = true;
        }
        ImGui_ImplSdl_NewFrame(window);
		//ImGui::ShowTestWindow();


		if (ImGui::BeginMainMenuBar())
		{
			if (ImGui::BeginMenu("File"))
			{
                if (ImGui::MenuItem("Load image")) { chooseLoadImg(); }
				ImGui::Separator();
				if (ImGui::MenuItem("Save .png")) { savepng(); }
				if (ImGui::MenuItem("Save .scr")) { savescr(); }
				if (ImGui::MenuItem("Save .h")) { saveh(); }
				if (ImGui::MenuItem("Save .inc")) { saveinc(); }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::MenuItem("Attribute/bitmap")) { gWindowAttribBitmap = !gWindowAttribBitmap; }
				if (ImGui::MenuItem("Histogram")) { gWindowHistograms = !gWindowHistograms; }
				if (ImGui::MenuItem("Modifier palette")) { gWindowModifierPalette = !gWindowModifierPalette; }
				if (ImGui::MenuItem("Zoomed output")) { gWindowZoomedOutput = !gWindowZoomedOutput;  }
				if (ImGui::MenuItem("Options")) { gWindowOptions = !gWindowOptions; }
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Modifier"))
			{
				if (ImGui::MenuItem("Open modifier palette")) { gWindowModifierPalette = !gWindowModifierPalette; }
				ImGui::Separator();
				if (ImGui::MenuItem("Add RGB modifier")) { addModifier(new RGBModifier); }
				if (ImGui::MenuItem("Add HSV modifier")) { addModifier(new HSVModifier); }
				if (ImGui::MenuItem("Add YIQ modifier")) { addModifier(new YIQModifier); }
				if (ImGui::MenuItem("Add Contrast modifier")) { addModifier(new ContrastModifier); }
				if (ImGui::MenuItem("Add Noise modifier")) { addModifier(new NoiseModifier); }
				if (ImGui::MenuItem("Add Ordered Dither modifier")) { addModifier(new OrderedDitherModifier); }
				if (ImGui::MenuItem("Add Error Diffusion Dither modifier")) { addModifier(new ErrorDiffusionDitherModifier); } 
				ImGui::EndMenu();
			}		
			if (ImGui::BeginMenu("Help"))
			{
				if (ImGui::MenuItem("About")) { gWindowAbout = !gWindowAbout; }
				ImGui::Separator();
				if (ImGui::MenuItem("Show help")) { gWindowHelp= !gWindowHelp; }

				ImGui::EndMenu();
			}
			ImGui::EndMainMenuBar();
		}

		if (gWindowModifierPalette)
		{
			if (ImGui::Begin("Add Modifiers", &gWindowModifierPalette, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				if (ImGui::Button("RGB", ImVec2(-1, 0))) { addModifier(new RGBModifier); }
				if (ImGui::Button("HSV", ImVec2(-1, 0))) { addModifier(new HSVModifier); }
				if (ImGui::Button("YIQ", ImVec2(-1, 0))) { addModifier(new YIQModifier); }
				if (ImGui::Button("Contrast", ImVec2(-1, 0))) { addModifier(new ContrastModifier); }
				if (ImGui::Button("Noise", ImVec2(-1, 0))) { addModifier(new NoiseModifier); }
				if (ImGui::Button("Ordered Dither", ImVec2(-1, 0))) { addModifier(new OrderedDitherModifier); }
				// widest button defines the window width, so we can't set it to "auto size"
				if (ImGui::Button("Error Diffusion Dither" /*, ImVec2(-1, 0)*/)) { addModifier(new ErrorDiffusionDitherModifier); }
			}
			ImGui::End();
		}


		if (gWindowAbout)
		{
			ImGui::SetNextWindowSize(ImVec2(400, 300));
			if (ImGui::Begin("About Image Spectrumizer " VERSION, &gWindowAbout, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGui::TextWrapped(
					"Image Spectrumizer\n"
					"\n"
					"Sources available at:\n"
					"https://github.com/jarikomppa/img2spec\n"
					"\n"
					"Copyright(c) 2015 Jari Komppa, http://iki.fi/sol\n"
					"\n"
					"This software is provided 'as-is', without any express or implied "
					"warranty.In no event will the authors be held liable for any damages "
					"arising from the use of this software.\n"
					"\n"
					"Permission is granted to anyone to use this software for any purpose, "
					"including commercial applications, and to alter it and redistribute it "
					"freely, subject to the following restrictions :\n"
					"\n"
					"1. The origin of this software must not be misrepresented; you must not "
					"claim that you wrote the original software.If you use this software "
					"in a product, an acknowledgement in the product documentation would be "
					"appreciated but is not required.\n"
					"2. Altered source versions must be plainly marked as such, and must not be "
					"misrepresented as being the original software.\n"
					"3. This notice may not be removed or altered from any source distribution.\n"
					"-----------\n"
					"\n"
					"Uses Ocornut's ImGui library, from:\n"
					"https://github.com/ocornut/imgui\n"
					"\n"
					"The MIT License(MIT)\n"
					"\n"
					"Copyright(c) 2014 - 2015 Omar Cornut and ImGui contributors\n"
					"\n"
					"Permission is hereby granted, free of charge, to any person obtaining a copy "
					"of this software and associated documentation files(the \"Software\"), to deal "
					"in the Software without restriction, including without limitation the rights "
					"to use, copy, modify, merge, publish, distribute, sublicense, and / or sell "
					"copies of the Software, and to permit persons to whom the Software is "
					"furnished to do so, subject to the following conditions :\n"
					"\n"
					"The above copyright notice and this permission notice shall be included in all "
					"copies or substantial portions of the Software.\n"
					"\n"
					"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR "
					"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, "
					"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE "
					"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER "
					"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, "
					"OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE "
					"SOFTWARE.\n"
					"\n"
					"-----------\n"
					"Uses various STB libraries from:\n"
					"https://github.com/nothings/stb/\n"
					"by Sean Barrett, under public domain\n");

			}
			ImGui::End();
		}

		if (gWindowHelp)
		{
			ImGui::SetNextWindowSize(ImVec2(400, 300));
			if (ImGui::Begin("Halp!", &gWindowHelp, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGui::TextWrapped(
					"Tips:\n"
					"----\n"
					"- The window can be resized.\n"
					"- Various helper windows can be opened from the window menu.\n"
					"- Don't hesistate to play with conversion options.\n"
					"\n"
					"Typical workflow:\n"
					"----------------"
					"\n"
					"1. Load an image (file->load image)\n"
					"2. Add modifiers (modifiers->...)\n"
					"3. (optionally) open options (window->options) and change conversion options\n"
					"4. Tweak modifiers until result is acceptable\n"
					"5. Save result (file->save ...)\n"
					"\n"
					"File formats:\n"
					"------------\n"
					"The file formats for scr, h and inc are basically the same. Bitmap (in spectrum screen "
					"order) is followed by attribute data. In case of non-standard cell sizes, the attribute "
					"data is bigger, but still in linear order - it is up to the code that uses the data to "
					"figure out how to use it.\n"
					"\n"
					".scr is a binary format.\n"
					"\n"
					".h is C array, only data is included, so typical use would be:\n"
					"  const myimagedata[]= {\n"
					"  #include \"myimagedata.h\"\n"
					"  };\n"
					"\n"
					".inc is assember .db lines.\n"
					"\n"
					);
			}
			ImGui::End();
		}


		if (gWindowOptions)
		{
			if (ImGui::Begin("Options", &gWindowOptions, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				ImGui::Combo("Attribute order", &gOptAttribOrder, "Make bitmap pretty\0Make bitmap compressable\0");
				ImGui::Combo("Bright attributes", &gOptBright, "Only dark\0Prefer dark\0Normal\0Prefer bright\0Only bright\0");
				ImGui::Combo("Paper attribute", &gOptPaper, "Optimal\0Black\0Blue\0Red\0Purple\0Green\0Cyan\0Yellow\0White\0");
				ImGui::Combo("Attribute cell size", &gOptCellSize, "8x8 (standard)\08x4 (bicolor)\08x2\08x1\0");
				ImGui::Separator();
				ImGui::SliderInt("Zoomed window zoom factor", &gOptZoom, 1, 8);
				ImGui::Combo("Zoomed window style", &gOptZoomStyle, "Normal\0Separated cells\0");
				ImGui::Separator();
				ImGui::Combo("Bitmap order when saving", &gOptScreenOrder, "Linear order\0Spectrum video RAM order\0");
			}
			ImGui::End();
		}

		if (gWindowAttribBitmap)
		{
			if (ImGui::Begin("Attrib/bitmap", &gWindowAttribBitmap, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				gen_attr_bitm();
				ImGui::Image((ImTextureID)gTextureAttr, picsize); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::Image((ImTextureID)gTextureBitm, picsize); ImGui::SameLine(); ImGui::Text(" ");
			}
			ImGui::End();
		}

		if (gWindowZoomedOutput)
		{
			if (ImGui::Begin("Zoomed output", &gWindowZoomedOutput, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				if (gOptZoomStyle == 1)
				{
					ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(1, 1));
					int i, j;
					int cellht = 8 >> gOptCellSize;
					int ymax = 24 << gOptCellSize;
					for (i = 0; i < ymax; i++)
					{
						for (j = 0; j < 32; j++)
						{
							ImGui::Image(
								(ImTextureID)gTextureSpec, 
								ImVec2(8.0f * gOptZoom, (float)cellht * gOptZoom),
								ImVec2((8 / 256.0f) * (j + 0), (cellht / 192.0f) * (i + 0)),
								ImVec2((8 / 256.0f) * (j + 1), (cellht / 192.0f) * (i + 1)));
							
							if (j != 31)
							{
								ImGui::SameLine();
							}
						}
					}
					ImGui::PopStyleVar();
				}
				else
				{
					ImGui::Image((ImTextureID)gTextureSpec, ImVec2(256.0f * gOptZoom, 192.0f * gOptZoom));
				}

			}
			ImGui::End();
		}

		if (gWindowHistograms)
		{
			if (ImGui::Begin("Histograms", &gWindowHistograms, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
			{
				calc_histogram(gBitmapOrig);
				ImGui::PlotHistogram("###hist1", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist2", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist3", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
				calc_histogram(gBitmapProc);
				ImGui::PlotHistogram("###hist4", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist5", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist6", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
				calc_histogram(gBitmapSpec);
				ImGui::PlotHistogram("###hist7", gHistogramR, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist8", gHistogramG, 256, 0, 0, 0, 1024, ImVec2(256, 32)); ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
				ImGui::PlotHistogram("###hist9", gHistogramB, 256, 0, 0, 0, 1024, ImVec2(256, 32));
			}
			ImGui::End();
		}
		
		ImGui::SetNextWindowContentSize(ImVec2(828, 512));
		ImGui::Begin("Image", 0, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize);	

		
		ImVec2 tex_screen_pos = ImGui::GetCursorScreenPos();

		ImGui::Image((ImTextureID)gTextureSpec, picsize);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			float focus_sz = 32.0f;
			float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > 256 - focus_sz) focus_x = 256 - focus_sz;
			float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > 192 - focus_sz) focus_y = 192 - focus_sz;
			ImVec2 uv0 = ImVec2((focus_x) / 256.0f, (focus_y) / 192.0f);
			ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 256.0f, (focus_y + focus_sz) / 192.0f);
			ImGui::Image((ImTextureID)gTextureSpec, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
			ImGui::EndTooltip();
		}
		ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
		tex_screen_pos = ImGui::GetCursorScreenPos();
		ImGui::Image((ImTextureID)gTextureProc, picsize); 
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			float focus_sz = 32.0f;
			float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > 256 - focus_sz) focus_x = 256 - focus_sz;
			float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > 192 - focus_sz) focus_y = 192 - focus_sz;
			ImVec2 uv0 = ImVec2((focus_x) / 256.0f, (focus_y) / 192.0f);
			ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 256.0f, (focus_y + focus_sz) / 192.0f);
			ImGui::Image((ImTextureID)gTextureProc, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
			ImGui::EndTooltip();
		}
		ImGui::SameLine(); ImGui::Text(" "); ImGui::SameLine();
		tex_screen_pos = ImGui::GetCursorScreenPos();
		ImGui::Image((ImTextureID)gTextureOrig, picsize);
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			float focus_sz = 32.0f;
			float focus_x = ImGui::GetMousePos().x - tex_screen_pos.x - focus_sz * 0.5f; if (focus_x < 0.0f) focus_x = 0.0f; else if (focus_x > 256 - focus_sz) focus_x = 256 - focus_sz;
			float focus_y = ImGui::GetMousePos().y - tex_screen_pos.y - focus_sz * 0.5f; if (focus_y < 0.0f) focus_y = 0.0f; else if (focus_y > 192 - focus_sz) focus_y = 192 - focus_sz;
			ImVec2 uv0 = ImVec2((focus_x) / 256.0f, (focus_y) / 192.0f);
			ImVec2 uv1 = ImVec2((focus_x + focus_sz) / 256.0f, (focus_y + focus_sz) / 192.0f);
			ImGui::Image((ImTextureID)gTextureOrig, ImVec2(128, 128), uv0, uv1, ImColor(255, 255, 255, 255), ImColor(255, 255, 255, 128));
			ImGui::EndTooltip();
		}


		ImGui::BeginChild("Modifiers");
		modifier_ui();
		ImGui::EndChild();
		ImGui::End();	

		process_image();
		spectrumize_image();

		glBindTexture(GL_TEXTURE_2D, gTextureProc);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)gBitmapProc);
		glBindTexture(GL_TEXTURE_2D, gTextureSpec);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 192, 0, GL_RGBA, GL_UNSIGNED_BYTE, (GLvoid*)gBitmapSpec);


        // Rendering
        glViewport(0, 0, (int)ImGui::GetIO().DisplaySize.x, (int)ImGui::GetIO().DisplaySize.y);
        glClearColor(clear_color.x, clear_color.y, clear_color.z, clear_color.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui::Render();
        SDL_GL_SwapWindow(window);
    }

    // Cleanup
    ImGui_ImplSdl_Shutdown();
    SDL_GL_DeleteContext(glcontext);  
	SDL_DestroyWindow(window);
	SDL_Quit();

    return 0;
}
