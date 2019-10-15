/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
// tr_shade.c

#include "tr_local.h" 

/*

  THIS ENTIRE FILE IS BACK END

  This file deals with applying shaders to surface data in the tess struct.
*/

/*
==================
R_DrawElements

Optionally performs our own glDrawElements that looks for strip conditions
instead of using the single glDrawElements call that may be inefficient
without compiled vertex arrays.
==================
*/
static void R_DrawElements( int numIndexes, const glIndex_t *indexes ) {
	vglDrawObjects(GL_TRIANGLES, numIndexes, GL_TRUE);
}


/*
=============================================================

SURFACE SHADERS

=============================================================
*/

shaderCommands_t	tess;
static qboolean	setArraysOnce;

/*
=================
R_BindAnimatedImage

=================
*/
static void R_BindAnimatedImage( textureBundle_t *bundle ) {
	int64_t index;

	if ( bundle->isVideoMap ) {
		ri.CIN_RunCinematic(bundle->videoMapHandle);
		ri.CIN_UploadCinematic(bundle->videoMapHandle);
		return;
	}

	if ( bundle->numImageAnimations <= 1 ) {
		GL_Bind( bundle->image[0] );
		return;
	}

	// it is necessary to do this messy calc to make sure animations line up
	// exactly with waveforms of the same frequency
	index = tess.shaderTime * bundle->imageAnimationSpeed * FUNCTABLE_SIZE;
	index >>= FUNCTABLE_SIZE2;

	if ( index < 0 ) {
		index = 0;	// may happen with shader time offsets
	}

	while ( index >= bundle->numImageAnimations ) {
		index -= bundle->numImageAnimations;
	}

	GL_Bind( bundle->image[ index ] );
}

/*
================
DrawTris

Draws triangle outlines for debugging
================
*/
static void DrawTris (shaderCommands_t *input) {
	GL_Bind( tr.whiteImage );
	qglColor4f (1,1,1, 1);

	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );
	if (!use_pgl) qglDepthRange( 0, 0 );

	qglDisableClientState (GL_COLOR_ARRAY);
	qglDisableClientState (GL_TEXTURE_COORD_ARRAY);

	float *vertices = gVertexBuffer;
	int i;
	for (i=0;i<input->numIndexes;i++){
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	
	//->if (qglLockArraysEXT) {
	//->	qglLockArraysEXT(0, input->numVertexes);
	//->	GLimp_LogComment( "glLockArraysEXT\n" );
	//->}

	R_DrawElements( input->numIndexes, input->indexes );

	//->if (qglUnlockArraysEXT) {
	//->	qglUnlockArraysEXT();
	//->	GLimp_LogComment( "glUnlockArraysEXT\n" );
	//->}
	if (!use_pgl) qglDepthRange( 0, 1 );
}


/*
================
DrawNormals

Draws vertex normals for debugging
================
*/
static void DrawNormals (shaderCommands_t *input) {
	int		i;
	vec3_t	temp;

	GL_Bind( tr.whiteImage );
	qglColor4f (1,1,1, 1);
	if (!use_pgl) qglDepthRange( 0, 0 );	// never occluded
	GL_State( GLS_POLYMODE_LINE | GLS_DEPTHMASK_TRUE );

	float *vertices = gVertexBuffer;
	for (i = 0 ; i < input->numVertexes ; i++) {
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
		VectorMA (input->xyz[input->indexes[i]], 2, input->normal[input->indexes[i]], temp);
		memcpy(gVertexBuffer, temp, sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	vglDrawObjects(GL_LINES, input->numVertexes * 2, GL_TRUE);

	if (!use_pgl) qglDepthRange( 0, 1 );
}

/*
==============
RB_BeginSurface

We must set some things up before beginning any tesselation,
because a surface may be forced to perform a RB_End due
to overflow.
==============
*/
void RB_BeginSurface( shader_t *shader, int fogNum ) {

	shader_t *state = (shader->remappedShader) ? shader->remappedShader : shader;

	tess.numIndexes = 0;
	tess.numVertexes = 0;
	tess.shader = state;
	tess.fogNum = fogNum;
	tess.dlightBits = 0;		// will be OR'd in by surface functions
	tess.xstages = state->stages;
	tess.numPasses = state->numUnfoggedPasses;
	tess.currentStageIteratorFunc = state->optimalStageIteratorFunc;

	tess.shaderTime = backEnd.refdef.floatTime - tess.shader->timeOffset;
	if (tess.shader->clampTime && tess.shaderTime >= tess.shader->clampTime) {
		tess.shaderTime = tess.shader->clampTime;
	}


}

/*
===================
DrawMultitextured

output = t0 * t1 or t0 + t1

t0 = most upstream according to spec
t1 = most downstream according to spec
===================
*/
static void DrawMultitextured( shaderCommands_t *input, int stage ) {
	shaderStage_t	*pStage;

	pStage = tess.xstages[stage];

	GL_State( pStage->stateBits );

	// this is an ugly hack to work around a GeForce driver
	// bug with multitexture and clip planes
	if ( backEnd.viewParms.isPortal ) {
		qglPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	}

	//
	// base
	//
	GL_SelectTexture( 0 );
	float *texcoord = gTexCoordBuffer;
	int i;
	for (i = 0 ; i < input->numIndexes ; i++) {
		memcpy(gTexCoordBuffer, input->svars.texcoords[0][input->indexes[i]], sizeof(vec2_t));
		gTexCoordBuffer += 2;
	}
	vglTexCoordPointerMapped(texcoord);

	R_BindAnimatedImage( &pStage->bundle[0] );
	R_DrawElements( input->numIndexes, input->indexes );
	
	//
	// lightmap/secondary pass
	//
	GL_SelectTexture( 1 );
	//->qglEnable( GL_TEXTURE_2D );
	//->qglEnableClientState( GL_TEXTURE_COORD_ARRAY );

	//->if ( r_lightmap->integer ) {
	//->	GL_TexEnv( GL_REPLACE );
	//->} else {
	//->	GL_TexEnv( tess.shader->multitextureEnv );
	//->}

	texcoord = gTexCoordBuffer;
	for (i = 0 ; i < input->numIndexes ; i++) {
		memcpy(gTexCoordBuffer, input->svars.texcoords[1][input->indexes[i]], sizeof(vec2_t));
		gTexCoordBuffer += 2;
	}
	vglTexCoordPointerMapped(texcoord);
	R_BindAnimatedImage( &pStage->bundle[1] );
	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//qglDisableClientState( GL_TEXTURE_COORD_ARRAY );
	//->qglDisable( GL_TEXTURE_2D );

	GL_SelectTexture( 0 );
}



/*
===================
ProjectDlightTexture

Perform dynamic lighting with another rendering pass
===================
*/
static void ProjectDlightTexture_scalar( void ) {
	int		i, l;
	vec3_t	origin;
	float	*texCoords;
	float	*colors;
	byte	clipBits[SHADER_MAX_VERTEXES];
	float	texCoordsArray[SHADER_MAX_VERTEXES][2];
	float	colorArray[SHADER_MAX_VERTEXES][4];
	glIndex_t	hitIndexes[SHADER_MAX_INDEXES];
	int		numIndexes;
	float	scale;
	float	radius;
	vec3_t	floatColor;
	float	modulate = 0.0f;

	if ( !backEnd.refdef.num_dlights ) {
		return;
	}

	for ( l = 0 ; l < backEnd.refdef.num_dlights ; l++ ) {
		dlight_t	*dl;

		if ( !( tess.dlightBits & ( 1 << l ) ) ) {
			continue;	// this surface definitely doesn't have any of this light
		}
		texCoords = texCoordsArray[0];
		colors = colorArray[0];

		dl = &backEnd.refdef.dlights[l];
		VectorCopy( dl->transformed, origin );
		radius = dl->radius;
		scale = 1.0f / radius;

		if(r_greyscale->integer)
		{
			float luminance;

			luminance = LUMA(dl->color[0], dl->color[1], dl->color[2]) * 255.0f;
			floatColor[0] = floatColor[1] = floatColor[2] = luminance;
		}
		else if(r_greyscale->value)
		{
			float luminance;
			
			luminance = LUMA(dl->color[0], dl->color[1], dl->color[2]) * 255.0f;
			floatColor[0] = LERP(dl->color[0] * 255.0f, luminance, r_greyscale->value);
			floatColor[1] = LERP(dl->color[1] * 255.0f, luminance, r_greyscale->value);
			floatColor[2] = LERP(dl->color[2] * 255.0f, luminance, r_greyscale->value);
		}
		else
		{
			floatColor[0] = dl->color[0] * 255.0f;
			floatColor[1] = dl->color[1] * 255.0f;
			floatColor[2] = dl->color[2] * 255.0f;
		}

		for ( i = 0 ; i < tess.numVertexes ; i++, texCoords += 2, colors += 4 ) {
			int		clip = 0;
			vec3_t	dist;
			
			VectorSubtract( origin, tess.xyz[i], dist );

			backEnd.pc.c_dlightVertexes++;

			texCoords[0] = 0.5f + dist[0] * scale;
			texCoords[1] = 0.5f + dist[1] * scale;

			if( !r_dlightBacks->integer &&
					// dist . tess.normal[i]
					( dist[0] * tess.normal[i][0] +
					dist[1] * tess.normal[i][1] +
					dist[2] * tess.normal[i][2] ) < 0.0f ) {
				clip = 63;
			} else {
				if ( texCoords[0] < 0.0f ) {
					clip |= 1;
				} else if ( texCoords[0] > 1.0f ) {
					clip |= 2;
				}
				if ( texCoords[1] < 0.0f ) {
					clip |= 4;
				} else if ( texCoords[1] > 1.0f ) {
					clip |= 8;
				}
				texCoords[0] = texCoords[0];
				texCoords[1] = texCoords[1];

				// modulate the strength based on the height and color
				if ( dist[2] > radius ) {
					clip |= 16;
					modulate = 0.0f;
				} else if ( dist[2] < -radius ) {
					clip |= 32;
					modulate = 0.0f;
				} else {
					dist[2] = Q_fabs(dist[2]);
					if ( dist[2] < radius * 0.5f ) {
						modulate = 1.0f;
					} else {
						modulate = 2.0f * (radius - dist[2]) * scale;
					}
				}
			}
			clipBits[i] = clip;
			colors[0] = (float)(ri.ftol(floatColor[0] * modulate)) / 255.0f;
			colors[1] = (float)(ri.ftol(floatColor[1] * modulate)) / 255.0f;
			colors[2] = (float)(ri.ftol(floatColor[2] * modulate)) / 255.0f;
			colors[3] = 1.0f;
		}

		// build a list of triangles that need light
		numIndexes = 0;
		for ( i = 0 ; i < tess.numIndexes ; i += 3 ) {
			int		a, b, c;

			a = tess.indexes[i];
			b = tess.indexes[i+1];
			c = tess.indexes[i+2];
			if ( clipBits[a] & clipBits[b] & clipBits[c] ) {
				continue;	// not lighted
			}
			hitIndexes[numIndexes] = a;
			hitIndexes[numIndexes+1] = b;
			hitIndexes[numIndexes+2] = c;
			numIndexes += 3;
		}

		if ( !numIndexes ) {
			continue;
		}
		
		qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
		qglEnableClientState( GL_COLOR_ARRAY );

		float *texcoord = gTexCoordBuffer;
		float *colorbuf = gColorBuffer;
		for (i = 0 ; i < numIndexes ; i++) {
			memcpy(gTexCoordBuffer, texCoordsArray[hitIndexes[i]], sizeof(vec2_t));
			gTexCoordBuffer += 2;
			memcpy(gColorBuffer, colorArray[hitIndexes[i]], sizeof(vec4_t));
			gColorBuffer += 4;
		}
		vglColorPointerMapped(GL_FLOAT, colorbuf);
		vglTexCoordPointerMapped(texcoord);

		GL_Bind( tr.dlightImage );
		// include GLS_DEPTHFUNC_EQUAL so alpha tested surfaces don't add light
		// where they aren't rendered
		if ( dl->additive ) {
			GL_State( GLS_SRCBLEND_ONE | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		else {
			GL_State( GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ONE | GLS_DEPTHFUNC_EQUAL );
		}
		R_DrawElements( numIndexes, hitIndexes );
		backEnd.pc.c_totalIndexes += numIndexes;
		backEnd.pc.c_dlightIndexes += numIndexes;
	}
}

static void ProjectDlightTexture( void ) {
	ProjectDlightTexture_scalar();
}

/*
===================
RB_FogPass

Blends a fog texture on top of everything else
===================
*/
static void RB_FogPass( void ) {
	fog_t		*fog;
	int			i;

	qglEnableClientState( GL_COLOR_ARRAY );
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);

	fog = tr.world->fogs + tess.fogNum;

	for ( i = 0; i < tess.numVertexes; i++ ) {
		* ( int * )&tess.svars.colors[i] = fog->colorInt;
	}

	RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[0] );

	GL_Bind( tr.fogImage );

	if ( tess.shader->fogPass == FP_EQUAL ) {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA | GLS_DEPTHFUNC_EQUAL );
	} else {
		GL_State( GLS_SRCBLEND_SRC_ALPHA | GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA );
	}

	float *texcoord = gTexCoordBuffer;
	float *colorbuf = gColorBuffer;
	for (i = 0 ; i < tess.numIndexes ; i++) {
		memcpy(gColorBuffer, tess.svars.colors[tess.indexes[i]], sizeof(uint32_t));
		gColorBuffer[0] = (float)(tess.svars.colors[tess.indexes[i]][0]) / 255.0f;
		gColorBuffer[1] = (float)(tess.svars.colors[tess.indexes[i]][1]) / 255.0f;
		gColorBuffer[2] = (float)(tess.svars.colors[tess.indexes[i]][2]) / 255.0f;
		gColorBuffer[3] = (float)(tess.svars.colors[tess.indexes[i]][3]) / 255.0f;
		gTexCoordBuffer += 2;
		gColorBuffer += 4;
	}
	vglColorPointerMapped(GL_FLOAT, colorbuf);
	vglTexCoordPointerMapped(texcoord);
	
	R_DrawElements( tess.numIndexes, tess.indexes );
}

/*
===============
ComputeColors
===============
*/
static void ComputeColors( shaderStage_t *pStage )
{
	int		i;

	//
	// rgbGen
	//
	switch ( pStage->rgbGen )
	{
		case CGEN_IDENTITY:
			Com_Memset( tess.svars.colors, 0xff, tess.numVertexes * 4 );
			break;
		default:
		case CGEN_IDENTITY_LIGHTING:
			Com_Memset( tess.svars.colors, tr.identityLightByte, tess.numVertexes * 4 );
			break;
		case CGEN_LIGHTING_DIFFUSE:
			RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_EXACT_VERTEX:
			Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			break;
		case CGEN_CONST:
			for ( i = 0; i < tess.numVertexes; i++ ) {
				*(int *)tess.svars.colors[i] = *(int *)pStage->constantColor;
			}
			break;
		case CGEN_VERTEX:
			if ( tr.identityLight == 1 )
			{
				Com_Memcpy( tess.svars.colors, tess.vertexColors, tess.numVertexes * sizeof( tess.vertexColors[0] ) );
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = tess.vertexColors[i][0] * tr.identityLight;
					tess.svars.colors[i][1] = tess.vertexColors[i][1] * tr.identityLight;
					tess.svars.colors[i][2] = tess.vertexColors[i][2] * tr.identityLight;
					tess.svars.colors[i][3] = tess.vertexColors[i][3];
				}
			}
			break;
		case CGEN_ONE_MINUS_VERTEX:
			if ( tr.identityLight == 1 )
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = 255 - tess.vertexColors[i][0];
					tess.svars.colors[i][1] = 255 - tess.vertexColors[i][1];
					tess.svars.colors[i][2] = 255 - tess.vertexColors[i][2];
				}
			}
			else
			{
				for ( i = 0; i < tess.numVertexes; i++ )
				{
					tess.svars.colors[i][0] = ( 255 - tess.vertexColors[i][0] ) * tr.identityLight;
					tess.svars.colors[i][1] = ( 255 - tess.vertexColors[i][1] ) * tr.identityLight;
					tess.svars.colors[i][2] = ( 255 - tess.vertexColors[i][2] ) * tr.identityLight;
				}
			}
			break;
		case CGEN_FOG:
			{
				fog_t		*fog;

				fog = tr.world->fogs + tess.fogNum;

				for ( i = 0; i < tess.numVertexes; i++ ) {
					* ( int * )&tess.svars.colors[i] = fog->colorInt;
				}
			}
			break;
		case CGEN_WAVEFORM:
			RB_CalcWaveColor( &pStage->rgbWave, ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_ENTITY:
			RB_CalcColorFromEntity( ( unsigned char * ) tess.svars.colors );
			break;
		case CGEN_ONE_MINUS_ENTITY:
			RB_CalcColorFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
			break;
	}

	//
	// alphaGen
	//
	switch ( pStage->alphaGen )
	{
	case AGEN_SKIP:
		break;
	case AGEN_IDENTITY:
		if ( pStage->rgbGen != CGEN_IDENTITY ) {
			if ( ( pStage->rgbGen == CGEN_VERTEX && tr.identityLight != 1 ) ||
				 pStage->rgbGen != CGEN_VERTEX ) {
				for ( i = 0; i < tess.numVertexes; i++ ) {
					tess.svars.colors[i][3] = 0xff;
				}
			}
		}
		break;
	case AGEN_CONST:
		if ( pStage->rgbGen != CGEN_CONST ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = pStage->constantColor[3];
			}
		}
		break;
	case AGEN_WAVEFORM:
		RB_CalcWaveAlpha( &pStage->alphaWave, ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_LIGHTING_SPECULAR:
		RB_CalcSpecularAlpha( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ENTITY:
		RB_CalcAlphaFromEntity( ( unsigned char * ) tess.svars.colors );
		break;
	case AGEN_ONE_MINUS_ENTITY:
		RB_CalcAlphaFromOneMinusEntity( ( unsigned char * ) tess.svars.colors );
		break;
    case AGEN_VERTEX:
		if ( pStage->rgbGen != CGEN_VERTEX ) {
			for ( i = 0; i < tess.numVertexes; i++ ) {
				tess.svars.colors[i][3] = tess.vertexColors[i][3];
			}
		}
        break;
    case AGEN_ONE_MINUS_VERTEX:
        for ( i = 0; i < tess.numVertexes; i++ )
        {
			tess.svars.colors[i][3] = 255 - tess.vertexColors[i][3];
        }
        break;
	case AGEN_PORTAL:
		{
			unsigned char alpha;

			for ( i = 0; i < tess.numVertexes; i++ )
			{
				float len;
				vec3_t v;

				VectorSubtract( tess.xyz[i], backEnd.viewParms.or.origin, v );
				len = VectorLength( v );

				len /= tess.shader->portalRange;

				if ( len < 0 )
				{
					alpha = 0;
				}
				else if ( len > 1 )
				{
					alpha = 0xff;
				}
				else
				{
					alpha = len * 0xff;
				}

				tess.svars.colors[i][3] = alpha;
			}
		}
		break;
	}

	//
	// fog adjustment for colors to fade out as fog increases
	//
	if ( tess.fogNum )
	{
		switch ( pStage->adjustColorsForFog )
		{
		case ACFF_MODULATE_RGB:
			RB_CalcModulateColorsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_ALPHA:
			RB_CalcModulateAlphasByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_MODULATE_RGBA:
			RB_CalcModulateRGBAsByFog( ( unsigned char * ) tess.svars.colors );
			break;
		case ACFF_NONE:
			break;
		}
	}
	
	// if in greyscale rendering mode turn all color values into greyscale.
	if(r_greyscale->integer)
	{
		int scale;
		for(i = 0; i < tess.numVertexes; i++)
		{
			scale = LUMA(tess.svars.colors[i][0], tess.svars.colors[i][1], tess.svars.colors[i][2]);
 			tess.svars.colors[i][0] = tess.svars.colors[i][1] = tess.svars.colors[i][2] = scale;
		}
	}
	else if(r_greyscale->value)
	{
		float scale;
		
		for(i = 0; i < tess.numVertexes; i++)
		{
			scale = LUMA(tess.svars.colors[i][0], tess.svars.colors[i][1], tess.svars.colors[i][2]);
			tess.svars.colors[i][0] = LERP(tess.svars.colors[i][0], scale, r_greyscale->value);
			tess.svars.colors[i][1] = LERP(tess.svars.colors[i][1], scale, r_greyscale->value);
			tess.svars.colors[i][2] = LERP(tess.svars.colors[i][2], scale, r_greyscale->value);
		}
	}
}

/*
===============
ComputeTexCoords
===============
*/
static void ComputeTexCoords( shaderStage_t *pStage ) {
	int		i;
	int		b;

	for ( b = 0; b < NUM_TEXTURE_BUNDLES; b++ ) {
		int tm;

		//
		// generate the texture coordinates
		//
		switch ( pStage->bundle[b].tcGen )
		{
		case TCGEN_IDENTITY:
			Com_Memset( tess.svars.texcoords[b], 0, sizeof( float ) * 2 * tess.numVertexes );
			break;
		case TCGEN_TEXTURE:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][0][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][0][1];
			}
			break;
		case TCGEN_LIGHTMAP:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = tess.texCoords[i][1][0];
				tess.svars.texcoords[b][i][1] = tess.texCoords[i][1][1];
			}
			break;
		case TCGEN_VECTOR:
			for ( i = 0 ; i < tess.numVertexes ; i++ ) {
				tess.svars.texcoords[b][i][0] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[0] );
				tess.svars.texcoords[b][i][1] = DotProduct( tess.xyz[i], pStage->bundle[b].tcGenVectors[1] );
			}
			break;
		case TCGEN_FOG:
			RB_CalcFogTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_ENVIRONMENT_MAPPED:
			RB_CalcEnvironmentTexCoords( ( float * ) tess.svars.texcoords[b] );
			break;
		case TCGEN_BAD:
			return;
		}

		//
		// alter texture coordinates
		//
		for ( tm = 0; tm < pStage->bundle[b].numTexMods ; tm++ ) {
			switch ( pStage->bundle[b].texMods[tm].type )
			{
			case TMOD_NONE:
				tm = TR_MAX_TEXMODS;		// break out of for loop
				break;

			case TMOD_TURBULENT:
				RB_CalcTurbulentTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ENTITY_TRANSLATE:
				RB_CalcScrollTexCoords( backEnd.currentEntity->e.shaderTexCoord,
									 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCROLL:
				RB_CalcScrollTexCoords( pStage->bundle[b].texMods[tm].scroll,
										 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_SCALE:
				RB_CalcScaleTexCoords( pStage->bundle[b].texMods[tm].scale,
									 ( float * ) tess.svars.texcoords[b] );
				break;
			
			case TMOD_STRETCH:
				RB_CalcStretchTexCoords( &pStage->bundle[b].texMods[tm].wave, 
						               ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_TRANSFORM:
				RB_CalcTransformTexCoords( &pStage->bundle[b].texMods[tm],
						                 ( float * ) tess.svars.texcoords[b] );
				break;

			case TMOD_ROTATE:
				RB_CalcRotateTexCoords( pStage->bundle[b].texMods[tm].rotateSpeed,
										( float * ) tess.svars.texcoords[b] );
				break;

			default:
				ri.Error( ERR_DROP, "ERROR: unknown texmod '%d' in shader '%s'", pStage->bundle[b].texMods[tm].type, tess.shader->name );
				break;
			}
		}
	}
}

/*
** RB_IterateStagesGeneric
*/
static void RB_IterateStagesGeneric( shaderCommands_t *input )
{
	int stage;

	for ( stage = 0; stage < MAX_SHADER_STAGES; stage++ )
	{
		shaderStage_t *pStage = tess.xstages[stage];

		if ( !pStage )
		{
			break;
		}

		ComputeColors( pStage );
		ComputeTexCoords( pStage );

		//if ( !setArraysOnce )
		//{
			qglEnableClientState( GL_COLOR_ARRAY );
			float *colorbuf = gColorBuffer;
			int i;
			for (i = 0 ; i < input->numIndexes ; i++) {
				gColorBuffer[0] = (float)(input->svars.colors[input->indexes[i]][0]) / 255.0f;
				gColorBuffer[1] = (float)(input->svars.colors[input->indexes[i]][1]) / 255.0f;
				gColorBuffer[2] = (float)(input->svars.colors[input->indexes[i]][2]) / 255.0f;
				gColorBuffer[3] = (float)(input->svars.colors[input->indexes[i]][3]) / 255.0f;
				gColorBuffer += 4;
			}
			vglColorPointerMapped(GL_FLOAT, colorbuf);
		//}

		//
		// do multitexture
		//
		if ( pStage->bundle[1].image[0] != 0 )
		{
			DrawMultitextured( input, stage );
		}
		else
		{
			//if ( !setArraysOnce )
			//{
				float *texcoord = gTexCoordBuffer;
				int i;
				for (i = 0 ; i < input->numIndexes ; i++) {
					memcpy(gTexCoordBuffer, input->svars.texcoords[0][input->indexes[i]], sizeof(vec2_t));
					gTexCoordBuffer += 2;
				}
				vglTexCoordPointerMapped(texcoord);
			//}

			//
			// set state
			//
			R_BindAnimatedImage( &pStage->bundle[0] );

			GL_State( pStage->stateBits );

			//
			// draw
			//
			R_DrawElements( input->numIndexes, input->indexes );
		}
		// allow skipping out to show just lightmaps during development
		if ( r_lightmap->integer && ( pStage->bundle[0].isLightmap || pStage->bundle[1].isLightmap ) )
		{
			break;
		}
	}
}

/*
** RB_StageIteratorGeneric
*/
void RB_StageIteratorGeneric( void )
{
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	RB_DeformTessGeometry();
	//
	// log this call
	//
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorGeneric( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	// set polygon offset if necessary
	if ( shader->polygonOffset )
	{
		qglEnable( GL_POLYGON_OFFSET_FILL );
		qglPolygonOffset( r_offsetFactor->value, r_offsetUnits->value );
	}

	//
	// lock XYZ
	//
	float *vertices = gVertexBuffer;
	int i;
	for (i=0;i<input->numIndexes;i++){
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	
	//->if (qglLockArraysEXT)
	//->{
	//->	qglLockArraysEXT(0, input->numVertexes);
	//->	GLimp_LogComment( "glLockArraysEXT\n" );
	//->}

	//
	// call shader function
	//
	RB_IterateStagesGeneric( input );

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE
		&& !(tess.shader->surfaceFlags & (SURF_NODLIGHT | SURF_SKY) ) ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	// 
	// unlock arrays
	//
	//->if (qglUnlockArraysEXT) 
	//->{
	//->	qglUnlockArraysEXT();
	//->	GLimp_LogComment( "glUnlockArraysEXT\n" );
	//->}

	//
	// reset polygon offset
	//
	if ( shader->polygonOffset )
	{
		qglDisable( GL_POLYGON_OFFSET_FILL );
	}
}


/*
** RB_StageIteratorVertexLitTexture
*/
void RB_StageIteratorVertexLitTexture( void )
{
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	//
	// compute colors
	//
	RB_CalcDiffuseColor( ( unsigned char * ) tess.svars.colors );

	//
	// log this call
	//
	if ( r_logFile->integer ) 
	{
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorVertexLitTexturedUnfogged( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	//
	// set arrays and lock
	//
	qglEnableClientState( GL_COLOR_ARRAY);
	qglEnableClientState( GL_TEXTURE_COORD_ARRAY);

	float *colorbuf = gColorBuffer;
	float *texcoord = gTexCoordBuffer;
	float *vertices = gVertexBuffer;
	int i;
	for (i = 0 ; i < tess.numIndexes ; i++) {
		gColorBuffer[0] = (float)(tess.svars.colors[tess.indexes[i]][0]) / 255.0f;
		gColorBuffer[1] = (float)(tess.svars.colors[tess.indexes[i]][1]) / 255.0f;
		gColorBuffer[2] = (float)(tess.svars.colors[tess.indexes[i]][2]) / 255.0f;
		gColorBuffer[3] = (float)(tess.svars.colors[tess.indexes[i]][3]) / 255.0f;
		gColorBuffer += 4;
		memcpy(gTexCoordBuffer, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		gTexCoordBuffer += 2;
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglColorPointerMapped(GL_FLOAT, colorbuf);
	vglTexCoordPointerMapped(texcoord);
	vglVertexPointerMapped(vertices);

	//->if ( qglLockArraysEXT )
	//->{
	//->	qglLockArraysEXT(0, input->numVertexes);
	//->	GLimp_LogComment( "glLockArraysEXT\n" );
	//->}

	//
	// call special shade routine
	//
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	GL_State( tess.xstages[0]->stateBits );
	R_DrawElements( input->numIndexes, input->indexes );

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	// 
	// unlock arrays
	//
	//->if (qglUnlockArraysEXT) 
	//->{
	//->	qglUnlockArraysEXT();
	//->	GLimp_LogComment( "glUnlockArraysEXT\n" );
	//->}
}

//define	REPLACE_MODE

void RB_StageIteratorLightmappedMultitexture( void ) {
	shaderCommands_t *input;
	shader_t		*shader;

	input = &tess;
	shader = input->shader;

	//
	// log this call
	//
	if ( r_logFile->integer ) {
		// don't just call LogComment, or we will get
		// a call to va() every frame!
		GLimp_LogComment( va("--- RB_StageIteratorLightmappedMultitexture( %s ) ---\n", tess.shader->name) );
	}

	//
	// set face culling appropriately
	//
	GL_Cull( shader->cullType );

	//
	// set color, pointers, and lock
	//
	GL_State( GLS_DEFAULT );
	qglEnableClientState( GL_COLOR_ARRAY );
	int i;
	float *vertices = gVertexBuffer;
	for (i = 0 ; i < input->numIndexes ; i++) {
		memcpy(gVertexBuffer, input->xyz[input->indexes[i]], sizeof(vec3_t));
		gVertexBuffer += 3;
	}
	vglVertexPointerMapped(vertices);
	vglColorPointerMapped(GL_FLOAT, gColorBuffer255);

	//
	// select base stage
	//
	GL_SelectTexture( 0 );

	qglEnableClientState( GL_TEXTURE_COORD_ARRAY );
	R_BindAnimatedImage( &tess.xstages[0]->bundle[0] );
	
	float *texcoord = gTexCoordBuffer;
	for (i = 0 ; i < tess.numIndexes ; i++) {
		memcpy(gTexCoordBuffer, tess.texCoords[tess.indexes[i]][0], sizeof(vec2_t));
		gTexCoordBuffer += 2;
	}
	vglTexCoordPointerMapped(texcoord);
	R_DrawElements( input->numIndexes, input->indexes );
	
	//
	// configure second stage
	//
	GL_SelectTexture( 1 );
	//->qglEnable( GL_TEXTURE_2D );
	//->if ( r_lightmap->integer ) {
	//->	GL_TexEnv( GL_REPLACE );
	//->} else {
	//->	GL_TexEnv( GL_MODULATE );
	//->}
	R_BindAnimatedImage( &tess.xstages[0]->bundle[1] );
	texcoord = gTexCoordBuffer;
	for (i = 0 ; i < tess.numIndexes ; i++) {
		memcpy(gTexCoordBuffer, tess.texCoords[tess.indexes[i]][1], sizeof(vec2_t));
		gTexCoordBuffer += 2;
	}
	vglTexCoordPointerMapped(texcoord);

	//
	// lock arrays
	//
	//->if ( qglLockArraysEXT ) {
	//->	qglLockArraysEXT(0, input->numVertexes);
	//->	GLimp_LogComment( "glLockArraysEXT\n" );
	//->}

	R_DrawElements( input->numIndexes, input->indexes );

	//
	// disable texturing on TEXTURE1, then select TEXTURE0
	//
	//->qglDisable( GL_TEXTURE_2D );
	//->qglDisableClientState( GL_TEXTURE_COORD_ARRAY );

	GL_SelectTexture( 0 );
#ifdef REPLACE_MODE
	GL_TexEnv( GL_MODULATE );
	qglShadeModel( GL_SMOOTH );
#endif

	// 
	// now do any dynamic lighting needed
	//
	if ( tess.dlightBits && tess.shader->sort <= SS_OPAQUE ) {
		ProjectDlightTexture();
	}

	//
	// now do fog
	//
	if ( tess.fogNum && tess.shader->fogPass ) {
		RB_FogPass();
	}

	//
	// unlock arrays
	//
	//->if ( qglUnlockArraysEXT ) {
	//->	qglUnlockArraysEXT();
	//->	GLimp_LogComment( "glUnlockArraysEXT\n" );
	//->}
}

/*
** RB_EndSurface
*/
void RB_EndSurface( void ) {
	shaderCommands_t *input;

	input = &tess;

	if (input->numIndexes == 0) {
		return;
	}

	if (input->indexes[SHADER_MAX_INDEXES-1] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_INDEXES hit");
	}	
	if (input->xyz[SHADER_MAX_VERTEXES-1][0] != 0) {
		ri.Error (ERR_DROP, "RB_EndSurface() - SHADER_MAX_VERTEXES hit");
	}

	if ( tess.shader == tr.shadowShader ) {
		RB_ShadowTessEnd();
		return;
	}

	// for debugging of sort order issues, stop rendering after a given sort value
	if ( r_debugSort->integer && r_debugSort->integer < tess.shader->sort ) {
		return;
	}

	//
	// update performance counters
	//
	backEnd.pc.c_shaders++;
	backEnd.pc.c_vertexes += tess.numVertexes;
	backEnd.pc.c_indexes += tess.numIndexes;
	backEnd.pc.c_totalIndexes += tess.numIndexes * tess.numPasses;

	//
	// call off to shader specific tess end function
	//
	tess.currentStageIteratorFunc();

	//
	// draw debugging stuff
	//
	if ( r_showtris->integer ) {
		DrawTris (input);
	}
	if ( r_shownormals->integer ) {
		DrawNormals (input);
	}
	// clear shader so we can tell we don't have any unclosed surfaces
	tess.numIndexes = 0;

	GLimp_LogComment( "----------\n" );
}

