// *************************************************************************************************
//
// Horde3D
//   Next-Generation Graphics Engine
// --------------------------------------
// Copyright (C) 2006-2009 Nicolas Schulz
//
// This software is distributed under the terms of the Eclipse Public License v1.0.
// A copy of the license may be obtained at: http://www.eclipse.org/legal/epl-v10.html
//
// *************************************************************************************************

/*	Title: Horde3D Utility Library */

#pragma once

#include "Horde3D.h"

#ifndef DLL
#	if defined( WIN32 ) || defined( _WINDOWS )
#		define DLL extern "C" __declspec( dllimport )
#	else
#  if defined( __GNUC__ ) && __GNUC__ >= 4
#   define DLL extern "C" __attribute__ ((visibility("default")))
#  else
#		define DLL extern "C"
#  endif
#	endif
#endif


/*	Topic: Introduction
		Some words about the Utility Library.
	
	Horde3D has a simple core API which offers all the functionality needed to control the engine. The
	engine core is intended to be as generic as possible to make the complete system lightweight and clean.
	Nevertheless, it is sometimes useful to have more specific functions in order to increase productivity.
	For this reason the Utility Library is offered. It has some very handy functions that can help
	to make your life easier.
*/


/* Group: Typedefs and constants */
/*	Constants: Predefined constants
	H3DUTMaxStatMode  - Maximum stat mode number supported in showFrameStats
*/
const int H3DUTMaxStatMode = 2;


/*	Group: General functions */
/*	Function: h3dutFreeMem
		Frees memory allocated by the Utility Library.
	
	Details:
		This utility function frees the memory that was allocated by another function of the Utility Library.
	
	Parameters:
		ptr  - address of a pointer that references to memory allocated by the Utility Library
		
	Returns:
		nothing
*/
DLL void h3dutFreeMem( char **ptr );

/*	Function: h3dutDumpMessages
		Writes all messages in the queue to a log file.
	
	Details:
		This utility function pops all messages from the message queue and writes them to a HTML formated
		log file 'Horde3D_Log.html'.
	
	Parameters:
		none
		
	Returns:
		true in case of success, otherwise false
*/
DLL bool h3dutDumpMessages();


/*	Group: OpenGL-related functions */
/* Function: h3dutInitOpenGL
		Initializes OpenGL.
	
	Details:
		This utility function initializes an OpenGL rendering context in a specified window component.
		
		*Currently this function is only available on Windows platforms.*
	
	Parameters:
		hDC  - handle to device context for which OpenGL context shall be created
		
	Returns:
		true in case of success, otherwise false
*/
DLL bool h3dutInitOpenGL( int hDC );

/* Function: h3dutReleaseOpenGL
		Releases OpenGL.
	
	Details:
		This utility function destroys the previously created OpenGL rendering context.
		
		*Currently this function is only available on Windows platforms.*
	
	Parameters:
		none
		
	Returns:
		nothing
*/
DLL void h3dutReleaseOpenGL();

/* Function: h3dutSwapBuffers
		Displays the rendered image on the screen.
	
	Details:
		This utility function displays the image rendered to the previously initialized OpenGL context
		on the screen by copying it from the backbuffer to the frontbuffer.
		
		*Currently this function is only available on Windows platforms.*
	
	Parameters:
		none
		
	Returns:
		nothing
*/
DLL void h3dutSwapBuffers();


/*	Group: Resource management */
/* Function: h3dutGetResourcePath
		*Deprecated*
		Returns  the search path of a resource type.
	
	Details:
		This function returns the search path of a specified resource type.

	The function is now marked as deprecated since it is better practice to make all paths
	relative to the content directory.
	
	Parameters:
		type  - type of resource
		
	Returns:
		pointer to the search path string
*/
DLL const char *h3dutGetResourcePath( int type );

/* Function: h3dutSetResourcePath
		*Deprecated*
		Sets the search path for a resource type.

	Details:
		This function sets the search path for a specified resource type.
	
	The function is now marked as deprecated since it is better practice to make all paths
	relative to the content directory.
	
	Parameters:
		type  - type of resource
		path  - path where the resources can be found ((back-)slashes at end are removed)
		
	Returns:
		nothing
*/
DLL void h3dutSetResourcePath( int type, const char *path );

/* Function: h3dutLoadResourcesFromDisk
		Loads previously added resources from a data drive.
	
	Details:
		This utility function loads previously added and still unloaded resources from the specified
		directories on a data drive. Several search paths can be specified using the pipe character (|)
		as separator. All resource names are directly converted to filenames and the function tries to
		find them in the specified directories using the given order of the search paths.
	
	Parameters:
		contentDir  - directories where data is located on the drive ((back-)slashes at end are removed)
		
	Returns:
		false if at least one resource could not be loaded, otherwise true
*/
DLL bool h3dutLoadResourcesFromDisk( const char *contentDir );

/* Function: h3dutCreateGeometryRes
		Creates a Geometry resource from specified vertex data.
	
	Details:
		This utility function allocates and initializes a Geometry resource
		with the specified vertex attributes and indices. The optional tangent space
		data (normal, tangent, bitangent) is encoded as int16, where -1.0 maps to
		-32'767 and 1.0f to +32'767.
	
	Parameters:
		name               - unique name of the new Geometry resource 
		numVertices        - number of vertices
		numTriangleIndices - number of vertex indices
		posData            - vertex positions (xyz)
		indexData          - indices defining triangles
		normalData         - normals xyz (optional, can be NULL)
		tangentData        - tangents xyz (optional, can be NULL)
		bitangentData      - bitangents xyz (required if tangents specified, otherwise NULL)
		texData1           - first texture coordinate uv set (optional, can be NULL)
		texData2           - second texture coordinate uv set (optional, can be NULL)
		
	Returns:
		handle to new Geometry resource or 0 in case of failure
*/
DLL H3DRes h3dutCreateGeometryRes( const char *name, int numVertices, int numTriangleIndices, 
								   float *posData, unsigned int *indexData, short *normalData,
								   short *tangentData, short *bitangentData, 
								   float *texData1, float *texData2 );

/* Function: h3dutCreateTGAImage
		Creates a TGA image in memory.
	
	Details:
		This utility function allocates memory at the pointer outData and creates a TGA image from the
		specified pixel data. The dimensions of the image have to be specified as well as the bit depth.
		The created TGA-image-data can be used as Texture2D or TexureCube resource in the engine.
		
		*Note: The memory allocated by this routine has to freed manually using the freeMem function.*
	
	Parameters:
		pixels   - pointer to pixel source data in BGR(A) format from which TGA-image is constructed;
		           memory layout: pixel with position (x, y) in image (origin of image is lower left
		           corner) has memory location (y * width + x) * (bpp / 8) in pixels array
		width    - width of source image
		height   - height of source image
		bpp      - color bit depth of source data (valid values: 24, 32)
		outData  - address of a pointer to which the address of the created memory block is written
		outSize  - variable to which to size of the created memory block is written
		
	Returns:
		false if at least one resource could not be loaded, otherwise true
*/
DLL bool h3dutCreateTGAImage( const unsigned char *pixels, int width, int height, int bpp,
                              char **outData, int *outSize );

/*	Group: Utils */
/* Function: h3dutScreenshot
		Writes the content of the backbuffer to a tga file.
	
	Details:
		This function reads back the content of the backbuffer and writes it to a tga file with the
		specified filename and path.
	
	Parameters:
		filename  - filename and path of the output tga file
		
	Returns:
		true if the file could be written, otherwise false
*/
DLL bool h3dutScreenshot( const char *filename );


/*	Group: Scene graph */
/* Function: h3dutPickRay
		Calculates the ray originating at the specified camera and window coordinates
	
	Details:
		This utility function takes normalized window coordinates (ranging from 0 to 1 with the
		origin being the bottom left corner of the window) and returns ray origin and direction for the
		given camera. The function is especially useful for selecting objects by clicking
		on them.
	
	Parameters:
		cameraNode  - camera used for picking
		nwx, nwy    - normalized window coordinates
		ox, oy, oz  - calculated ray origin
		dx, dy, dz  - calculated ray direction
		
	Returns:
		nothing
*/
DLL void h3dutPickRay( H3DNode cameraNode, float nwx, float nwy, float *ox, float *oy, float *oz,
                       float *dx, float *dy, float *dz );

/* Function: h3dutPickNode
		Returns the scene node which is at the specified window coordinates.
	
	Details:
		This utility function takes normalized window coordinates (ranging from 0 to 1 with the
		origin being the bottom left corner of the window) and returns the scene node which is
		visible at that location. The function is especially useful for selecting objects by clicking
		on them. Currently picking is only working for Meshes.
	
	Parameters:
		cameraNode  - camera used for picking
		nwx, nwy    - normalized window coordinates

	Returns:
		handle of picked node or 0 if no node was hit
*/
DLL H3DNode h3dutPickNode( H3DNode cameraNode, float nwx, float nwy );

/*	Group: Overlays */
/* Function: h3dutShowText
		Shows text on the screen using a font texture.
	
	Details:
		This utility function uses overlays to display a text string at a specified position on the screen.
		The font texture of the specified font material has to be a regular 16x16 grid containing all
		ASCII characters in row-major order.
	
	Parameters:
		text              - text string to be displayed
		x, y              - position of the lower left corner of the first character;
		                    for more details on coordinate system see overlay documentation
		size              - size (scale) factor of the font
		colR, colG, colB  - font color
		fontMaterialRes   - font material resource used for rendering
		
	Returns:
		nothing
*/
DLL void h3dutShowText( const char *text, float x, float y, float size,
                        float colR, float colG, float colB, H3DRes fontMaterialRes );

/* Function: h3dutShowFrameStats
		Shows frame statistics on the screen.
	
	Details:
		This utility function displays an info box with statistics for the current frame on the screen.
		Since the statistic counters are reset after the call, the function should be called exactly once
		per frame to obtain correct values.
	
	Parameters:
		fontMaterialRes	  - font material resource used for drawing text
		panelMaterialRes  - material resource used for drawing info box
		mode              - display mode, specifying which data is shown (<= MaxStatMode)
		
	Returns:
		nothing
*/
DLL void h3dutShowFrameStats( H3DRes fontMaterialRes, H3DRes panelMaterialRes, int mode );
