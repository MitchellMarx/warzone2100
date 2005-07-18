/*
 * Map.c
 *
 * Utility functions for the map data structure.
 *
 */
#include <stdio.h>

/* map line printf's */
//#define DEBUG_GROUP1
#include <assert.h>
#include "frame.h"
#include "frameint.h"
#define DEFINE_MAPINLINE	// defines the inline functions in this module
#include "map.h"
#include "gtime.h"
#include "hci.h"
#include "projectile.h"
// chnaged line	- alex
#include "display3d.h"
#include "lighting.h"
// end of chnage - alex
#include "game.h"

#include "environ.h"
#include "advvis.h"


#include "gateway.h"
#include "wrappers.h"

#include "fractions.h"

BOOL	bDoneWater = FALSE;
BOOL	nearLand(UDWORD x, UDWORD y);
void	mapWaterProcess( void );
MAPTILE *tileHasNeighbourType(UDWORD x, UDWORD y, TYPE_OF_TERRAIN type);
//void	mapFreeTilesAndStrips( void );

//scroll min and max values
SDWORD		scrollMinX, scrollMaxX, scrollMinY, scrollMaxY;

/* Structure definitions for loading and saving map data */
typedef struct _map_save_header
{
	STRING		aFileType[4];
	UDWORD		version;
	UDWORD		width;
	UDWORD		height;
} MAP_SAVEHEADER;


#define SAVE_MAP_V2 \
	UWORD		texture; \
	UBYTE		height


typedef struct _map_save_tilev2
{
	SAVE_MAP_V2;
} MAP_SAVETILEV2;
	
typedef struct _map_save_tilev1
{
	UDWORD		texture;
	UBYTE		type;
	UBYTE		height;
} MAP_SAVETILEV1;

typedef struct _map_save_tile
{
	SAVE_MAP_V2;
} MAP_SAVETILE;


typedef struct _gateway_save_header
{
	UDWORD		version;
	UDWORD		numGateways;
} GATEWAY_SAVEHEADER;

typedef struct _gateway_save
{
	UBYTE	x0,y0,x1,y1;
} GATEWAY_SAVE;


typedef struct _zonemap_save_header_v1 {
	UWORD version;
	UWORD numZones;
} ZONEMAP_SAVEHEADER_V1;


typedef struct _zonemap_save_header {
	UWORD version;
	UWORD numZones;
	UWORD numEquivZones;
	UWORD pad;
} ZONEMAP_SAVEHEADER;


/* Floating point type for the aaLine */
// AAFLOAT's are interchangable with the FRACT type used in fractions.h
//
// - I couldn't bring myself to rewrite John's execellent fixed/floating point code
//

typedef float AAFLOAT;



/* Sanity check definitions for the save struct file sizes */
#define SAVE_HEADER_SIZE	16
#define SAVE_TILE_SIZE		3
#define SAVE_TILE_SIZEV1	6
#define SAVE_TILE_SIZEV2	3

/* Floating point constants for aaLine */

/* Windows fpu version */
#define AA_ZERO				0.0F
#define AA_ONE				1.0F
#define AA_NINES			0.999
#define AA_HALF				0.5
#define AA_PMAX				1.0//\0.71			// Maximum perpendicular distance from line center

#define AADIV(a,b)			((AAFLOAT)(a)/(AAFLOAT)b)			// Floating point divide
#define AAMUL(a,b)			((AAFLOAT)(a)*(AAFLOAT)(b))			// Floating point multiply

/* Access the root table */
#define AARTFUNC(x) (aAARootTbl[ (UDWORD)((x) * ROOT_TABLE_SIZE) ])



// Maximun expected return value from get height
#define	MAX_HEIGHT			(256 * ELEVATION_SCALE)	

/* Number of entries in the sqrt(1/(1+x*x)) table for aaLine */
#define	ROOT_TABLE_SIZE		1024	

/* aaLine direction bits and tables */
#define DIR_STEEP			1  /* set when abs(dy) > abs(dx) */
#define DIR_NEGY			2  /* set whey dy < 0 */

/* Defines to access the map for aaLine */
#define PIXADDR(x,y)		mapTile(x,y)
//#define PIXINC(dx,dy)		((dy << mapShift) + dx) //width no longer a power of 2
#define PIXINC(dx,dy)		((dy * mapWidth) + dx)



/* The size and contents of the map */
UDWORD	mapWidth = 0, mapHeight = 0;
MAPTILE	*psMapTiles = NULL;

/* The shift on the y coord when calculating into the map */
//UDWORD	mapShift = 0;

/* The map tiles generated by map calc line */
TILE_COORD			*aMapLinePoints = NULL;
//static UDWORD		maxLinePoints = 0;

/* The sqrt(1/(1+x*x)) table for aaLine */
AAFLOAT		*aAARootTbl;


/* pixel increment values for aaLine                        */
/*   -- assume PIXINC(dx,dy) is a macro such that:          */
/*   PIXADDR(x0,y0) + PIXINC(dx,dy) = PIXADDR(x0+dx,y0+dy)  */
//static int adj_pixinc[4];
//static int diag_pixinc[4];
//static int orth_pixinc[4];

/* Initialise the sqrt(1/(1+x*x)) lookup table */
//static void mapRootTblInit(void);

/* Initialise the pixel offset tabels for aaLine */
//static void mapPixTblInit(void);


/* Look up table that returns the terrain type of a given tile texture */
UBYTE terrainTypes[MAX_TILE_TEXTURES];



#define GETTILE_TEXTURE(tile) (tile->texture)



#define GETTILE_TEXTURE2(tile) (tile->texture)


/* pointer to a load map function - depends on version */
BOOL (*pLoadMapFunc)(UBYTE *pFileData, UDWORD fileSize);

MAPTILE *GetCurrentMap(void)	// returns a pointer to the current loaded map data
{
	return(psMapTiles);
}

/* Create a new map of a specified size */
BOOL mapNew(UDWORD width, UDWORD height)
{
//	UDWORD	numPoints;
	UDWORD	i;//, tmp, bitCount, widthShift;
	MAPTILE	*psTile;

	/* See if a map has already been allocated */
	if (psMapTiles != NULL)
	{
		/* Clear all the objects off the map and free up the map memory */
		freeAllDroids();
		freeAllStructs();
		freeAllFeatures();
		proj_FreeAllProjectiles();
//		FREE(psMapTiles);
//		mapFreeTilesAndStrips();
		FREE(aMapLinePoints);
		psMapTiles = NULL;
		aMapLinePoints = NULL;
	}

//	if (width > MAP_MAXWIDTH || height > MAP_MAXHEIGHT)
	if (width*height > MAP_MAXAREA)
	{
		DBERROR(("mapNew: map too large : %d %d\n",width,height));
		return FALSE;
	}

	//DON'T BOTHER ANYMORE
	/* Check the width is a power of 2 */
	/*bitCount = 0;
	tmp = width;
	widthShift = 0;
	for(i=0; i<32; i++)
	{
		if (tmp & 1)
		{
			bitCount ++;
			widthShift = i;
		}
		tmp = tmp >> 1;
	}
	if (bitCount != 1)
	{
		DBERROR(("mapNew: width must be a power of two"));
		return FALSE;
	}
	*/


	psMapTiles = (MAPTILE *)MALLOC(sizeof(MAPTILE) * width*height);
	if (psMapTiles == NULL)
	{
		DBERROR(("mapNew: Out of memory"));
		return FALSE;
	}
	memset(psMapTiles, 0, sizeof(MAPTILE) * width*height);

	mapWidth = width;
	mapHeight = height;
	
	for (i=0; i<MAX_TILE_TEXTURES; i++)
		{
			terrainTypes[i] = TER_SANDYBRUSH;
		}
	
	/* Calculate the shift to use on y when indexing into the map array - USES mapWidth NOW*/
//	mapShift = widthShift;

	/* Allocate a buffer for the LOS routines points */



/*	numPoints = iSQRT(mapWidth * mapWidth +  mapHeight * mapHeight) + 1;



	aMapLinePoints = (TILE_COORD *)MALLOC(sizeof(TILE_COORD) * numPoints);
	if (!aMapLinePoints)
	{
		DBERROR(("Out of memory"));
		return FALSE;
	}
	maxLinePoints = numPoints;
*/
	/* Initialise the root table for aaLine */
//	mapRootTblInit();
//	mapPixTblInit();

	intSetMapPos(mapWidth * TILE_UNITS/2, mapHeight * TILE_UNITS/2);

	/* Initialise the map terrain type */
	psTile = psMapTiles;
	/*
	for(i=mapWidth * mapHeight; i>0; i--)
	{
		psTile->type = TER_GRASS;
		psTile++;
	}
	*/

	//environInit();
    environReset();

	/*set up the scroll mins and maxs - set values to valid ones for a new map*/
	scrollMinX = scrollMinY = 0;
	scrollMaxX = mapWidth;
	scrollMaxY = mapHeight;
	return TRUE;
}

/* load the map data - for version 1 */
BOOL mapLoadV1(UBYTE *pFileData, UDWORD fileSize)
{
	UDWORD				i,j;
	MAP_SAVETILEV1		*psTileData;

	/* Load in the map data */
	psTileData = (MAP_SAVETILEV1 *)(pFileData + SAVE_HEADER_SIZE);
	for(i=0; i<mapWidth * mapHeight; i++)
	{

		UDWORD DataTexture;

		DataTexture= GETTILE_TEXTURE(psTileData);
		// get the texture number
		psMapTiles[i].texture = (UWORD)(DataTexture&0xffff);
		// get the flip bits
		psMapTiles[i].texture |= (UWORD)((DataTexture & 0xff000000) >> 16);
//		psMapTiles[i].type = psTileData->type;
		psMapTiles[i].height = psTileData->height;
//		psMapTiles[i].onFire = 0;
		// Changed line - alex
//		psMapTiles[i].rippleIndex = (UBYTE) (i%RIP_SIZE);
		//end of change - alex
		for (j=0; j<MAX_PLAYERS; j++)
		{
//			psMapTiles[i].tileVisible[j]=FALSE;
			psMapTiles[i].tileVisBits =(UBYTE)(( (psMapTiles[i].tileVisBits) &~ (UBYTE)(1<<j) ));

		}
		psTileData = (MAP_SAVETILEV1 *)(((UBYTE *)psTileData) + SAVE_TILE_SIZEV1);
	}
	if (((UBYTE *)psTileData) - pFileData > (SDWORD)fileSize)
	{
		DBERROR(("mapLoad: unexpected end of file"));
		return FALSE;
	}

	if (!gwCreateNULLZoneMap())
	{
		return FALSE;
	}

	return TRUE;
}

/* load the map data - for version 1 */
BOOL mapLoadV2(UBYTE *pFileData, UDWORD fileSize)
{
	UDWORD				i,j;
	MAP_SAVETILEV2		*psTileData;

	/* Load in the map data */
	psTileData = (MAP_SAVETILEV2 *)(pFileData + SAVE_HEADER_SIZE);
	for(i=0; i< mapWidth * mapHeight; i++)
	{

		psMapTiles[i].texture = GETTILE_TEXTURE2(psTileData);  



//		psMapTiles[i].type = psTileData->type;
		psMapTiles[i].height = psTileData->height;
//		psMapTiles[i].onFire = 0;
		// Changed line - alex
//		psMapTiles[i].rippleIndex = (UBYTE) (i%RIP_SIZE);
		//end of change - alex
		for (j=0; j<MAX_PLAYERS; j++)
		{
//			psMapTiles[i].tileVisible[j]=FALSE;
			psMapTiles[i].tileVisBits =(UBYTE)(( (psMapTiles[i].tileVisBits) &~ (UBYTE)(1<<j) ));
		}
		psTileData = (MAP_SAVETILEV2 *)(((UBYTE *)psTileData) + SAVE_TILE_SIZE);
	}

	if (((UBYTE *)psTileData) - pFileData > (SDWORD)fileSize)
	{
		DBERROR(("mapLoad: unexpected end of file"));
		return FALSE;
	}

	if (!gwCreateNULLZoneMap())
	{
		return FALSE;
	}

	return TRUE;
}


/* load the map data - for version 3 */
BOOL mapLoadV3(UBYTE *pFileData, UDWORD fileSize)
{
	UDWORD				i,j;
	MAP_SAVETILEV2		*psTileData;
	GATEWAY_SAVEHEADER	*psGateHeader;
	GATEWAY_SAVE		*psGate;
	ZONEMAP_SAVEHEADER	*psZoneHeader;
	UWORD ZoneSize;
	UBYTE *pZone;
	UBYTE *pDestZone;

	/* Load in the map data */
	psTileData = (MAP_SAVETILEV2 *)(pFileData + SAVE_HEADER_SIZE);
	for(i=0; i< mapWidth * mapHeight; i++)
	{

		psMapTiles[i].texture = GETTILE_TEXTURE2(psTileData);  



//		psMapTiles[i].type = psTileData->type;
		psMapTiles[i].height = psTileData->height;
//		psMapTiles[i].onFire = 0;
		// Changed line - alex
//		psMapTiles[i].rippleIndex = (UBYTE) (i%RIP_SIZE);
		//end of change - alex
		for (j=0; j<MAX_PLAYERS; j++)
		{
//			psMapTiles[i].tileVisible[j]=FALSE;
			psMapTiles[i].tileVisBits =(UBYTE)(( (psMapTiles[i].tileVisBits) &~ (UBYTE)(1<<j) ));
		}
		psTileData = (MAP_SAVETILEV2 *)(((UBYTE *)psTileData) + SAVE_TILE_SIZE);
	}


	psGateHeader = (GATEWAY_SAVEHEADER*)psTileData;
	psGate = (GATEWAY_SAVE*)(psGateHeader+1);

	ASSERT((psGateHeader->version == 1,"Invalid gateway version"));

	for(i=0; i<psGateHeader->numGateways; i++) {
		if (!gwNewGateway(psGate->x0,psGate->y0, psGate->x1,psGate->y1)) {
			DBERROR(("mapLoadV3: Unable to add gateway"));
			return FALSE;
		}
		psGate++;
	}

//#ifndef PSX
//	if (!gwProcessMap())
//	{
//		return FALSE;
//	}
//
//	if ((psGateways != NULL) &&
//		!gwGenerateLinkGates())
//	{
//		return FALSE;
//	}
//#else
	psZoneHeader = (ZONEMAP_SAVEHEADER*)psGate;

	ASSERT(( (psZoneHeader->version == 1) || (psZoneHeader->version == 2),
			"Invalid zone map version"));

	if(!gwNewZoneMap()) {
		return FALSE;
	}

	// This is a bit nasty but should work fine.
	if(psZoneHeader->version == 1) {
		// version 1 so add the size of a version 1 header.
		pZone = ((UBYTE*)psZoneHeader) + sizeof(ZONEMAP_SAVEHEADER_V1);
	} else {
		// version 2 so add the size of a version 2 header.
		pZone = ((UBYTE*)psZoneHeader) + sizeof(ZONEMAP_SAVEHEADER);
	}

	for(i=0; i<psZoneHeader->numZones; i++) {
		ZoneSize = *((UWORD*)(pZone));

		pDestZone = gwNewZoneLine(i,ZoneSize);

		if(pDestZone == NULL) {
			return FALSE;
		}

		for(j=0; j<ZoneSize; j++) {
			pDestZone[j] = pZone[2+j];
		}

		pZone += ZoneSize+2;
	}

	// Version 2 has the zone equivelancy lists tacked on the end.
	if(psZoneHeader->version == 2) {

		if(psZoneHeader->numEquivZones > 0) {
			// Load in the zone equivelance lists.
			if(!gwNewEquivTable(psZoneHeader->numEquivZones)) {
				DBERROR(("gwNewEquivTable failed"));
				return FALSE;
			}

			for(i=0; i<psZoneHeader->numEquivZones; i++) {
				if(*pZone != 0) {
					if(!gwSetZoneEquiv(i, (SDWORD)*pZone, pZone+1)) {
						DBERROR(("gwSetZoneEquiv failed"));
						return FALSE;
					}
				}
				pZone += ((UDWORD)*pZone)+1;
			}
		}
	}

	if (((UBYTE *)pZone) - pFileData > (SDWORD)fileSize)
	{
		DBERROR(("mapLoadV3: unexpected end of file"));
		return FALSE;
	}
//#endif

	LOADBARCALLBACK();	//	loadingScreenCallback();

	if ((apEquivZones != NULL) &&
		!gwGenerateLinkGates())
	{
		return FALSE;
	}

	LOADBARCALLBACK();	//	loadingScreenCallback();

	//add new map initialise
	if (!gwLinkGateways())
	{
		return FALSE;
	}

	LOADBARCALLBACK();	//	loadingScreenCallback();


#if defined(DEBUG) && !defined(PSX)
	gwCheckZoneSizes();
#endif

	return TRUE;
}



/* Initialise the map structure */
BOOL mapLoad(UBYTE *pFileData, UDWORD fileSize)
{
//	UDWORD				i;
//	UDWORD	tmp, bitCount, widthShift;
	UDWORD				width,height;
	MAP_SAVEHEADER		*psHeader;
	BOOL				mapAlloc;
//	UDWORD				i;

	/* Check the file type */
	psHeader = (MAP_SAVEHEADER *)pFileData;
	if (psHeader->aFileType[0] != 'm' || psHeader->aFileType[1] != 'a' ||
		psHeader->aFileType[2] != 'p' || psHeader->aFileType[3] != ' ')
	{
		DBERROR(("mapLoad: Incorrect file type"));
		FREE(pFileData);
		return FALSE;
	}

	/* Check the file version - deal with version 1 files */
	/* Check the file version */
	if (psHeader->version < VERSION_7)
	{
		DBERROR(("MapLoad: unsupported save format version %d",psHeader->version));
		FREE(pFileData);
		return FALSE;
	}
	else if (psHeader->version <= VERSION_9)
	{
		pLoadMapFunc = mapLoadV2;
	}
	else if (psHeader->version <= CURRENT_VERSION_NUM)
	{
		pLoadMapFunc = mapLoadV3;	// Includes gateway data for routing.
	}
	else
	{
		DBERROR(("MapLoad: undefined save format version %d",psHeader->version));
		FREE(pFileData);
		return FALSE;
	}

	/* Get the width and height */
	width = psHeader->width;
	height = psHeader->height;

//	if (width > MAP_MAXWIDTH || height > MAP_MAXHEIGHT)
	if (width*height > MAP_MAXAREA)
	{
		DBERROR(("mapLoad: map too large : %d %d\n",width,height));
		return FALSE;
	}

	//DON'T BOTHER ANYMORE
	/* Check the width is a power of 2 */
	/*bitCount = 0;
	tmp = width;
	widthShift = 0;
	for(i=0; i<32; i++)
	{
		if (tmp & 1)
		{
			bitCount ++;
			widthShift = i;
		}
		tmp = tmp >> 1;
	}
	if (bitCount != 1)
	{
		DBERROR(("mapLoad: width must be a power of two"));
		return FALSE;
	}
	*/

	/* See if this is the first time a map has been loaded */
	mapAlloc = TRUE;
	if (psMapTiles != NULL)
	{
		if (mapWidth == width && mapHeight == height)
		{
			mapAlloc = FALSE;
		}
		else
		{
			/* Clear all the objects off the map and free up the map memory */
			freeAllDroids();
			freeAllStructs();
			freeAllFeatures();
			proj_FreeAllProjectiles();
//			FREE(psMapTiles);
//			mapFreeTilesAndStrips();
			FREE(aMapLinePoints);
			psMapTiles = NULL;
			aMapLinePoints = NULL;
		}
	}
	
	/* Allocate the memory for the map */
	if (mapAlloc)
	{


		psMapTiles = (MAPTILE *)MALLOC(sizeof(MAPTILE) * width*height);
		if (psMapTiles == NULL)
		{
			DBERROR(("mapLoad: Out of memory"));
			return FALSE;
		}
		memset(psMapTiles, 0, sizeof(MAPTILE) * width*height);

	 

		mapWidth = width;
		mapHeight = height;

/*		a terrain type is loaded when necessary - so don't reset
		for (i=0; i<MAX_TILE_TEXTURES; i++)
		{
			terrainTypes[i] = TER_SANDYBRUSH;
		}*/
		/* Calculate the shift to use on y when indexing into the map array */
//		mapShift = widthShift;

		/* Allocate a buffer for the LOS routines points */

/*		numPoints = iSQRT(mapWidth * mapWidth +  mapHeight * mapHeight) + 1;



		aMapLinePoints = (TILE_COORD *)MALLOC(sizeof(TILE_COORD) * numPoints);
		if (!aMapLinePoints)
		{
			DBERROR(("Out of memory"));
			return FALSE;
		}
		maxLinePoints = numPoints;
*/
		/* Initialise the root table for aaLine */
//		mapRootTblInit();
//removed for NEW_SAVE //V11 Save
//		intSetMapPos(mapWidth * TILE_UNITS/2, mapHeight * TILE_UNITS/2);
	}

	//load in the map data itself
	pLoadMapFunc(pFileData, fileSize);

//	mapPixTblInit();

  	//environInit();
    environReset();


	/* set up the scroll mins and maxs - set values to valid ones for any new map */
	scrollMinX = scrollMinY = 0;
	scrollMaxX = mapWidth;
	scrollMaxY = mapHeight;
	
	return TRUE;
}


/* Save the map data */
BOOL mapSave(UBYTE **ppFileData, UDWORD *pFileSize)
{
	UDWORD	i;
	MAP_SAVEHEADER	*psHeader;
	MAP_SAVETILE	*psTileData;
	MAPTILE	*psTile;
	GATEWAY *psCurrGate;
	GATEWAY_SAVEHEADER *psGateHeader;
	GATEWAY_SAVE *psGate;
	ZONEMAP_SAVEHEADER *psZoneHeader;
	UBYTE *psZone;
	UBYTE *psLastZone;
	SDWORD	numGateways;

	// find the number of non water gateways
	numGateways = 0;
	for(psCurrGate = gwGetGateways(); psCurrGate; psCurrGate = psCurrGate->psNext)
	{
		if (!(psCurrGate->flags & GWR_WATERLINK))
		{
			numGateways += 1;
		}
	}


	/* Allocate the data buffer */
	*pFileSize = SAVE_HEADER_SIZE + mapWidth*mapHeight * SAVE_TILE_SIZE;
	// Add on the size of the gateway data.
	*pFileSize += sizeof(GATEWAY_SAVEHEADER) + sizeof(GATEWAY_SAVE)*numGateways;
	// Add on the size of the zone data header.
	*pFileSize += sizeof(ZONEMAP_SAVEHEADER);
	// Add on the size of the zone data.
	for(i=0; i<gwNumZoneLines(); i++) {
		*pFileSize += 2+gwZoneLineSize(i);
	}
	// Add on the size of the equivalency lists.
	for(i=0; i<(UDWORD)gwNumZones; i++) {
		*pFileSize += 1+aNumEquiv[i];
	}
	
	*ppFileData = (UBYTE *)MALLOC(*pFileSize);
	if (*ppFileData == NULL)
	{
		DBERROR(("Out of memory"));
		return FALSE;
	}

	/* Put the file header on the file */
	psHeader = (MAP_SAVEHEADER *)*ppFileData;
	psHeader->aFileType[0] = 'm';
	psHeader->aFileType[1] = 'a';
	psHeader->aFileType[2] = 'p';
	psHeader->aFileType[3] = ' ';
	psHeader->version = CURRENT_VERSION_NUM;
	psHeader->width = mapWidth;
	psHeader->height = mapHeight;

	/* Put the map data into the buffer */
	psTileData = (MAP_SAVETILE *)((UBYTE *)*ppFileData + SAVE_HEADER_SIZE);
	psTile = psMapTiles;
	for(i=0; i<mapWidth*mapHeight; i++)
	{

		// don't save the noblock flag as it gets set again when the objects are loaded
		psTileData->texture = (UWORD)(psTile->texture & (UWORD)~TILE_NOTBLOCKING);

		psTileData->height = psTile->height;

		psTileData = (MAP_SAVETILE *)((UBYTE *)psTileData + SAVE_TILE_SIZE);
		psTile ++;
	}

	// Put the gateway header.
	psGateHeader = (GATEWAY_SAVEHEADER*)psTileData;
	psGateHeader->version = 1;
	psGateHeader->numGateways = numGateways;

	psGate = (GATEWAY_SAVE*)(psGateHeader+1);

	i=0;
	// Put the gateway data.
	for(psCurrGate = gwGetGateways(); psCurrGate; psCurrGate = psCurrGate->psNext)
	{
		if (!(psCurrGate->flags & GWR_WATERLINK))
		{
			psGate->x0 = psCurrGate->x1;
			psGate->y0 = psCurrGate->y1;
			psGate->x1 = psCurrGate->x2;
			psGate->y1 = psCurrGate->y2;
			psGate++;
			i++;
		}
	}

	// Put the zone header.
	psZoneHeader = (ZONEMAP_SAVEHEADER*)psGate;
	psZoneHeader->version = 2;
	psZoneHeader->numZones =(UWORD)gwNumZoneLines();
	psZoneHeader->numEquivZones =(UWORD)gwNumZones;

	// Put the zone data.
	psZone = (UBYTE*)(psZoneHeader+1);
	for(i=0; i<gwNumZoneLines(); i++) {
		psLastZone = psZone;
		*((UWORD*)psZone) = (UWORD)gwZoneLineSize(i);
		psZone += sizeof(UWORD);
		memcpy(psZone,apRLEZones[i],gwZoneLineSize(i));
		psZone += gwZoneLineSize(i);
	}

	// Put the equivalency lists.
	if(gwNumZones > 0) {
		for(i=0; i<(UDWORD)gwNumZones; i++) {
			psLastZone = psZone;
			*psZone = aNumEquiv[i];
			psZone ++;
			if(aNumEquiv[i]) {
				memcpy(psZone,apEquivZones[i],aNumEquiv[i]);
				psZone += aNumEquiv[i];
			}
		}
	}
	
	ASSERT(( ( ((UDWORD)psLastZone) - ((UDWORD)*ppFileData) ) < *pFileSize,"Buffer overflow saving map"));

	return TRUE;
}

#if 0
/* Save the map data */
BOOL mapSaveMission(UBYTE **ppFileData, UDWORD *pFileSize)
{
	UDWORD	i;
	MAP_SAVEHEADER	*psHeader;
	MAP_SAVETILE	*psTileData;
	MAPTILE	*psTile;
	GATEWAY *psCurrGate;
	GATEWAY_SAVEHEADER *psGateHeader;
	GATEWAY_SAVE *psGate;
	ZONEMAP_SAVEHEADER *psZoneHeader;
	UBYTE *psZone;
	UBYTE *psLastZone;

	/* Allocate the data buffer */
	*pFileSize = SAVE_HEADER_SIZE + mission.mapWidth*mission.mapHeight * SAVE_TILE_SIZE;
	// Add on the size of the gateway data.
	*pFileSize += sizeof(GATEWAY_SAVEHEADER) + sizeof(GATEWAY_SAVE)*mission.gwNumGateways();
	// Add on the size of the zone data header.
	*pFileSize += sizeof(ZONEMAP_SAVEHEADER);
	// Add on the size of the zone data.
	for(i=0; i<gwNumZoneLines(); i++) {
		*pFileSize += 2+gwZoneLineSize(i);
	}
	// Add on the size of the equivalency lists.
	for(i=0; i<gwNumZones; i++) {
		*pFileSize += 1+aNumEquiv[i];
	}
	
	*ppFileData = (UBYTE *)MALLOC(*pFileSize);
	if (*ppFileData == NULL)
	{
		DBERROR(("Out of memory"));
		return FALSE;
	}

	/* Put the file header on the file */
	psHeader = (MAP_SAVEHEADER *)*ppFileData;
	psHeader->aFileType[0] = 'm';
	psHeader->aFileType[1] = 'a';
	psHeader->aFileType[2] = 'p';
	psHeader->aFileType[3] = ' ';
	psHeader->version = CURRENT_VERSION_NUM;
	psHeader->width = mapWidth;
	psHeader->height = mapHeight;

	/* Put the map data into the buffer */
	psTileData = (MAP_SAVETILE *)((UBYTE *)*ppFileData + SAVE_HEADER_SIZE);
	psTile = psMapTiles;
	for(i=0; i<mapWidth*mapHeight; i++)
	{

		psTileData->texture = psTile->texture;

		psTileData->height = psTile->height;

		psTileData = (MAP_SAVETILE *)((UBYTE *)psTileData + SAVE_TILE_SIZE);
		psTile ++;
	}

	// Put the gateway header.
	psGateHeader = (GATEWAY_SAVEHEADER*)psTileData;
	psGateHeader->version = 1;
	psGateHeader->numGateways = gwNumGateways();

	psGate = (GATEWAY_SAVE*)(psGateHeader+1);

	i=0;
	// Put the gateway data.
	for(psCurrGate = gwGetGateways(); psCurrGate; psCurrGate = psCurrGate->psNext)
	{
		psGate->x0 = psCurrGate->x1;
		psGate->y0 = psCurrGate->y1;
		psGate->x1 = psCurrGate->x2;
		psGate->y1 = psCurrGate->y2;
		psGate++;
		i++;
	}

	// Put the zone header.
	psZoneHeader = (ZONEMAP_SAVEHEADER*)psGate;
	psZoneHeader->version = 2;
	psZoneHeader->numZones = gwNumZoneLines();
	psZoneHeader->numEquivZones = gwNumZones;

	// Put the zone data.
	psZone = (UBYTE*)(psZoneHeader+1);
	for(i=0; i<gwNumZoneLines(); i++) {
		psLastZone = psZone;
		*((UWORD*)psZone) = gwZoneLineSize(i);
		psZone += sizeof(UWORD);
		memcpy(psZone,apRLEZones[i],gwZoneLineSize(i));
		psZone += gwZoneLineSize(i);
	}

	// Put the equivalency lists.
	if(gwNumZones > 0) {
		for(i=0; i<gwNumZones; i++) {
			psLastZone = psZone;
			*psZone = aNumEquiv[i];
			psZone ++;
			if(aNumEquiv[i]) {
				memcpy(psZone,apEquivZones[i],aNumEquiv[i]);
				psZone += aNumEquiv[i];
			}
		}
	}
	
	ASSERT(( ( ((UDWORD)psLastZone) - ((UDWORD)*ppFileData) ) < *pFileSize,"Buffer overflow saving map"));

	return TRUE;
}
#endif

/* Shutdown the map module */
BOOL mapShutdown(void)
{
	if(psMapTiles) {
		FREE(psMapTiles);
//		mapFreeTilesAndStrips();
	}
	psMapTiles = NULL;
//	mapWidth = mapHeight = mapShift = 0;
	mapWidth = mapHeight = 0;

/*	if(aMapLinePoints) {
		FREE(aMapLinePoints);
	}
	aMapLinePoints = NULL;

	if(aAARootTbl) {
		FREE(aAARootTbl);
	}
	aAARootTbl = NULL;
*/
	return TRUE;
}



/* work along a line on the map storing the points in aPoints.
 * The start and end points are in MAPTILE coordinates.
 */
void mapCalcLine(UDWORD startX, UDWORD startY,
				 UDWORD endX, UDWORD endY,
				 UDWORD *pNumPoints)
{
#if 0
	SDWORD		d, x,y, ax,ay, sx,sy, dx,dy;
	SDWORD		lineChange;
	MAPTILE		*psCurrTile;

	ASSERT(((startX < mapWidth) && (startY < mapHeight),
		"mapCalcLine: start point off map"));
	ASSERT(((endX < mapWidth) && (endY < mapHeight),
		"mapCalcLine: end point off map"));

	DBP1(("\nmapCalcLine: (%3d,%3d) -> (%3d,%3d)\n",
		startX,startY, endX,endY));

	/* Do some initial set up for the line */
	dx = endX - startX;
	dy = endY - startY;
	ax = abs(dx) << 1;
	ay = abs(dy) << 1;
	sx = dx < 0 ? -1 : 1;
	sy = dy < 0 ? -1 : 1;

	x = startX;
	y = startY;
	psCurrTile = psMapTiles + mapWidth * startY + startX;
	lineChange = dy < 0 ? -(SDWORD)mapWidth : (SDWORD)mapWidth;
	*pNumPoints = 0;
	if (ax > ay)
	{
		/* x dominant */
		d = ay - ax/2;
		FOREVER
		{
			DBP1(("(%3d, %3d)\n", x,y));
			aMapLinePoints[*pNumPoints].x = x;
			aMapLinePoints[*pNumPoints].y = y;
			aMapLinePoints[*pNumPoints].psTile = psCurrTile;
			(*pNumPoints)++;
			if (x == (SDWORD)endX)
			{
				/* Finished line - end loop */
				break;
			}
			if (d >= 0)
			{
				y = y + sy;
				d = d - ax;
				psCurrTile += lineChange;
				DBP1(("(%3d, %3d)\n", x,y));
				aMapLinePoints[*pNumPoints].x = x;
				aMapLinePoints[*pNumPoints].y = y;
				aMapLinePoints[*pNumPoints].psTile = psCurrTile;
				(*pNumPoints)++;
			}
			x = x + sx;
			d = d + ay;
			psCurrTile += sx;
		}
	}
	else
	{
		/* y dominant */
		d = ax - ay/2;
		FOREVER
		{
			DBP1(("(%3d, %3d)\n", x,y));
			aMapLinePoints[*pNumPoints].x = x;
			aMapLinePoints[*pNumPoints].y = y;
			aMapLinePoints[*pNumPoints].psTile = psCurrTile;
			(*pNumPoints)++;
			if (y == (SDWORD)endY)
			{
				/* Finished line - end loop */
				break;
			}
			if (d >= 0)
			{
				x = x + sx;
				d = d - ay;
				psCurrTile += sx;
				DBP1(("(%3d, %3d)\n", x,y));
				aMapLinePoints[*pNumPoints].x = x;
				aMapLinePoints[*pNumPoints].y = y;
				aMapLinePoints[*pNumPoints].psTile = psCurrTile;
				(*pNumPoints)++;
			}
			y = y + sy;
			d = d + ax;
			psCurrTile += lineChange;
		}
	}

	ASSERT((*pNumPoints <= maxLinePoints,
		"mapCalcLine: Too many points generated for buffer"));

#endif
}

/* Initialise the sqrt(1/(1+x*x)) lookup table */
void mapRootTblInit(void)
{
#if 0
	int bitCount,tmp,i;
	AAFLOAT		*pCell;
	AAFLOAT		nowval,incval;
	UDWORD		tablebits;
	UDWORD		tablecells;

	/* See if the table has already been set up */
	if (aAARootTbl)
	{
		return;
	}

	/* Sort out the table size */


	/* Check the width is a power of 2 */

//	tablebits = (UDWORD)( log(ROOT_TABLE_SIZE) / log(2) + AA_NINES );	// very unpleasant old code

	bitCount = 0;
	tmp = ROOT_TABLE_SIZE;
	tablebits = 0;
	for(i=0; i<32; i++)
	{
		if (tmp & 1)
		{
			bitCount ++;
			tablebits = i;
		}
		tmp = tmp >> 1;
	}

	ASSERT((bitCount==1,"ROOT_TABLE_SIZE not a power of 2"));		// ROOT_TABLE_SIZE must be a power of 2

	tablecells = (1 << tablebits) + 1;
	


	/* Allocate the table */
	aAARootTbl = MALLOC( tablecells * sizeof(AAFLOAT) );

	/* Set the table values */
//	incval = AADIV(AA_ONE,(tablecells - 1));	// This line is incorrect ... the second value is a constant not a fixed point value
	incval = AA_ONE/(tablecells - 1);	// This line is the correct value

	pCell = aAARootTbl;
	for(nowval = AA_ZERO; nowval < AA_ONE; nowval += incval)
	{
		*pCell++ = (AAFLOAT) fSQRT( AADIV(AA_ONE, (AA_ONE + AAMUL(nowval, nowval))) );
	}

	aAARootTbl[tablecells - 1] = (AAFLOAT) fSQRT( AA_HALF );

#endif
}

/* Initialise the pixel offset tabels for aaLine */
void mapPixTblInit(void)
{
#if 0
	/* pixel increment values for aaLine                        */
	/*   -- assume PIXINC(dx,dy) is a macro such that:          */
	/*   PIXADDR(x0,y0) + PIXINC(dx,dy) = PIXADDR(x0+dx,y0+dy)  */
	adj_pixinc[0] = PIXINC(1,0);
	adj_pixinc[1] = PIXINC(0,1);
	adj_pixinc[2] = PIXINC(1,0);
	adj_pixinc[3] = PIXINC(0,-1);

	diag_pixinc[0] = PIXINC(1,1);
	diag_pixinc[1] = PIXINC(1,1);
	diag_pixinc[2] = PIXINC(1,-1);
	diag_pixinc[3] = PIXINC(1,-1);

	orth_pixinc[0] = PIXINC(0,1);
	orth_pixinc[1] = PIXINC(1,0);
	orth_pixinc[2] = PIXINC(0,-1);
	orth_pixinc[3] = PIXINC(1,0);
#endif
}

/* Fill in the aa line */
void mapCalcAALine(SDWORD X1, SDWORD Y1,
				   SDWORD X2, SDWORD Y2,
				   UDWORD *pNumPoints)
{
#if 0
	SDWORD 	Bvar,		/* decision variable for Bresenham's */
    		Bainc,		/* adjacent-increment for 'Bvar' */
    		Bdinc;		/* diagonal-increment for 'Bvar' */
	AAFLOAT	Pmid,		/* perp distance at Bresenham's pixel */
   			Pnow,		/* perp distance at current pixel (ortho loop) */
   			Painc,		/* adjacent-increment for 'Pmid' */
   			Pdinc,		/* diagonal-increment for 'Pmid' */
   			Poinc;		/* orthogonal-increment for 'Pnow'--also equals 'k' */
	MAPTILE 	*mid_addr,	/* pixel address for Bresenham's pixel */
     		*now_addr,	/* pixel address for current pixel */
			*min_addr,	/* minimum address for clipping */
			*max_addr;	/* maximum address for clipping */
	SDWORD 	addr_ainc,	/* adjacent pixel address offset */
    		addr_dinc,	/* diagonal pixel address offset */
    		addr_oinc;	/* orthogonal pixel address offset */
	SDWORD  dx,dy,dir;	/* direction and deltas */
	AAFLOAT	slope;		/* slope of line */
	SDWORD	temp;

	/* rearrange ordering to force left-to-right */
	if 	( X1 > X2 )
  	{
		temp = X2;
		X2 = X1;
		X1 = temp;

		temp = Y2;
		Y2 = Y1;
		Y1 = temp;
	}

	/* init deltas */
	dx = X2 - X1;  /* guaranteed non-negative */
	dy = Y2 - Y1;


	/* calculate direction (slope category) */
	dir = 0;
	if ( dy < 0 )
	{
		dir |= DIR_NEGY;
		dy = -dy;
		min_addr = PIXADDR(X1,Y2);
		max_addr = PIXADDR(X2,Y1);
	}
	else
	{
		min_addr = PIXADDR(X1,Y1);
		max_addr = PIXADDR(X2,Y2);
	}
	if ( dy > dx )
	{
		dir |= DIR_STEEP;
		temp = dy;
		dy = dx;
		dx = temp;
	}

	/* init address stuff */
	mid_addr = PIXADDR(X1,Y1);
	addr_ainc = adj_pixinc[dir];
	addr_dinc = diag_pixinc[dir];
	addr_oinc = orth_pixinc[dir];

	/* perpendicular measures */
	slope = AADIV(dy,dx);

	Poinc = AARTFUNC( slope );
	Painc = AAMUL( slope, Poinc );
	Pdinc = Painc - Poinc;
	Pmid = AA_ZERO;


	/* init Bresenham's */
	Bainc = dy << 1;
	Bdinc = (dy-dx) << 1;
	Bvar = Bainc - dx;

	*pNumPoints = 0;
	do
  		{
  		/* do middle pixel */
		aMapLinePoints[*pNumPoints].psTile = mid_addr;
		(*pNumPoints)++;

  		/* go up orthogonally */
  		for (
      		Pnow = Poinc-Pmid,  now_addr = mid_addr+addr_oinc;
      		Pnow < AA_PMAX && *pNumPoints < maxLinePoints;
      		Pnow += Poinc,      now_addr += addr_oinc
      		)
		{
			if (now_addr >= min_addr && now_addr <= max_addr)
			{
				aMapLinePoints[*pNumPoints].psTile = now_addr;
				(*pNumPoints)++;
			}
		}

  		/* go down orthogonally */
  		for (
      		Pnow = Poinc+Pmid,  now_addr = mid_addr-addr_oinc;
      		Pnow < AA_PMAX && *pNumPoints < maxLinePoints;
      		Pnow += Poinc,      now_addr -= addr_oinc
      		)
		{
			if (now_addr >= min_addr && now_addr <= max_addr)
			{
				aMapLinePoints[*pNumPoints].psTile = now_addr;
				(*pNumPoints)++;
			}
		}


  		/* update Bresenham's */
  		if ( Bvar < 0 )
    		{
    		Bvar += Bainc;
    		mid_addr += addr_ainc;
    		Pmid += Painc;
    		}
  		else
    		{
    		Bvar += Bdinc;
    		mid_addr += addr_dinc;
    		Pmid += Pdinc;
    		}

  		--dx;
  		} while ( dx >= 0 && *pNumPoints < maxLinePoints);
#endif
}

/* Return linear interpolated height of x,y */
//extern SDWORD map_Height(UDWORD x, UDWORD y)
extern SWORD map_Height(UDWORD x, UDWORD y)
{
	SDWORD	retVal;
	UDWORD tileX, tileY, tileYOffset;
	SDWORD h0, hx, hy, hxy, wTL = 0, wTR = 0, wBL = 0, wBR = 0;
	//SDWORD	lowerHeightOffset,upperHeightOffset;
	SDWORD dx, dy, ox, oy;
	BOOL	bWaterTile = FALSE;
/*	ASSERT((x < (mapWidth << TILE_SHIFT),
		"mapHeight: x coordinate bigger than map width"));
	ASSERT((y < (mapHeight<< TILE_SHIFT),
		"mapHeight: y coordinate bigger than map height"));
*/
    x = x > SDWORD_MAX ? 0 : x;//negative SDWORD passed as UDWORD
    x = x >= (mapWidth << TILE_SHIFT) ? ((mapWidth-1) << TILE_SHIFT) : x;
    y = y > SDWORD_MAX ? 0 : y;//negative SDWORD passed as UDWORD
	y = y >= (mapHeight << TILE_SHIFT) ? ((mapHeight-1) << TILE_SHIFT) : y;

	/* Tile comp */
	tileX = x >> TILE_SHIFT;
	tileY = y >> TILE_SHIFT;
   
	/* Inter tile comp */
	ox = (x & (TILE_UNITS-1));
	oy = (y & (TILE_UNITS-1));


	if(TERRAIN_TYPE(mapTile(tileX,tileY)) == TER_WATER)
	{
		bWaterTile = TRUE;
		wTL = environGetValue(tileX,tileY)/2;
		wTR = environGetValue(tileX+1,tileY)/2;
		wBL = environGetValue(tileX,tileY+1)/2;
		wBR = environGetValue(tileX+1,tileY+1)/2;
		/*
		lowerHeightOffset = waves[(y%(MAX_RIPPLES-1))];
		upperHeightOffset = waves[((y%(MAX_RIPPLES-1))+1)];
		oy = (SDWORD)y - (SDWORD)(tileY << TILE_SHIFT);
		oy = TILE_UNITS - oy;
		dy = ((lowerHeightOffset - upperHeightOffset) * oy )/ TILE_UNITS;
		
		return((SEA_LEVEL + (dy*ELEVATION_SCALE)));
		*/
	}


	tileYOffset = (tileY * mapWidth);

//	ox = (SDWORD)x - (SDWORD)(tileX << TILE_SHIFT);
//	oy = (SDWORD)y - (SDWORD)(tileY << TILE_SHIFT);

	

	ASSERT((ox < TILE_UNITS, "mapHeight: x offset too big"));
	ASSERT((oy < TILE_UNITS, "mapHeight: y offset too big"));
	ASSERT((ox >= 0, "mapHeight: x offset too small"));
	ASSERT((oy >= 0, "mapHeight: y offset too small"));

	//different code for 4 different triangle cases
	if (psMapTiles[tileX + tileYOffset].texture & TILE_TRIFLIP)
	{
		if ((ox + oy) > TILE_UNITS)//tile split top right to bottom left object if in bottom right half
		{
			ox = TILE_UNITS - ox;
			oy = TILE_UNITS - oy;
			hy = psMapTiles[tileX + tileYOffset + mapWidth].height;
			hx = psMapTiles[tileX + 1 + tileYOffset].height;
			hxy= psMapTiles[tileX + 1 + tileYOffset + mapWidth].height;
			if(bWaterTile)
			{
				hy+=wBL;
				hx+=wTR;
				hxy+=wBR;
			}

			dx = ((hy - hxy) * ox )/ TILE_UNITS;
			dy = ((hx - hxy) * oy )/ TILE_UNITS;

			retVal = (SDWORD)(((hxy + dx + dy)) * ELEVATION_SCALE);
			ASSERT((retVal<MAX_HEIGHT,"Map height's gone weird!!!"));
			return ((SWORD)retVal);
		}
		else //tile split top right to bottom left object if in top left half
		{
			h0 = psMapTiles[tileX + tileYOffset].height;
			hy = psMapTiles[tileX + tileYOffset + mapWidth].height;
			hx = psMapTiles[tileX + 1 + tileYOffset].height;

			if(bWaterTile)
			{
				h0+=wTL;
				hy+=wBL;
				hx+=wTR;
			}
			dx = ((hx - h0) * ox )/ TILE_UNITS;
			dy = ((hy - h0) * oy )/ TILE_UNITS;

			retVal = (SDWORD)((h0 + dx + dy) * ELEVATION_SCALE);
			ASSERT((retVal<MAX_HEIGHT,"Map height's gone weird!!!"));
			return ((SWORD)retVal);
		}
	}
	else
	{
		if (ox > oy) //tile split topleft to bottom right object if in top right half
		{
			h0 = psMapTiles[tileX + tileYOffset].height;
			hx = psMapTiles[tileX + 1 + tileYOffset].height;
			hxy= psMapTiles[tileX + 1 + tileYOffset + mapWidth].height;

			if(bWaterTile)
			{
				h0+=wTL;
				hx+=wTR;
				hxy+=wBR;
			}
			dx = ((hx - h0) * ox )/ TILE_UNITS;
			dy = ((hxy - hx) * oy )/ TILE_UNITS;
			retVal = (SDWORD)(((h0 + dx + dy)) * ELEVATION_SCALE);
			ASSERT((retVal<MAX_HEIGHT,"Map height's gone weird!!!"));
			return ((SWORD)retVal);
		}
		else //tile split topleft to bottom right object if in bottom left half
		{
			h0 = psMapTiles[tileX + tileYOffset].height;
			hy = psMapTiles[tileX + tileYOffset + mapWidth].height;
			hxy = psMapTiles[tileX + 1 + tileYOffset + mapWidth].height;

			if(bWaterTile)
			{
				h0+=wTL;
				hy+=wBL;
				hxy+=wBR;
			}
			dx = ((hxy - hy) * ox )/ TILE_UNITS;
			dy = ((hy - h0) * oy )/ TILE_UNITS;

			retVal = (SDWORD)((h0 + dx + dy) * ELEVATION_SCALE);
			ASSERT((retVal<MAX_HEIGHT,"Map height's gone weird!!!"));
			return ((SWORD)retVal);
		}
	}
	return 0;
}

/* returns TRUE if object is above ground */
extern BOOL mapObjIsAboveGround( BASE_OBJECT *psObj )
{
	SDWORD	iZ,
			tileX = psObj->x >> TILE_SHIFT,
			tileY = psObj->y >> TILE_SHIFT,
			tileYOffset1 = (tileY * mapWidth),
			tileYOffset2 = ((tileY+1) * mapWidth),
			h1 = psMapTiles[tileYOffset1 + tileX    ].height,
			h2 = psMapTiles[tileYOffset1 + tileX + 1].height,
			h3 = psMapTiles[tileYOffset2 + tileX    ].height,
			h4 = psMapTiles[tileYOffset2 + tileX + 1].height;

	/* trivial test above */
	if ( (psObj->z > h1) && (psObj->z > h2) &&
		 (psObj->z > h3) && (psObj->z > h4)    )
	{
		return TRUE;
	}
	
	/* trivial test below */
	if ( (psObj->z <= h1) && (psObj->z <= h2) &&
		 (psObj->z <= h3) && (psObj->z <= h4)    )
	{
		return FALSE;
	}

	/* exhaustive test */
	iZ = map_Height( psObj->x, psObj->y );
	if ( psObj->z > iZ )
	{
		return TRUE;
	}
	else
	{
		return FALSE;
	}
}

/* returns the max and min height of a tile by looking at the four corners 
   in tile coords */
void getTileMaxMin(UDWORD x, UDWORD y, UDWORD *pMax, UDWORD *pMin)
{
	UDWORD	height, i, j;

	*pMin = TILE_MAX_HEIGHT;
	*pMax = TILE_MIN_HEIGHT;

	for (j=0; j < 2; j++)
	{
		for (i=0; i < 2; i++)
		{
			height = map_TileHeight(x+i, y+j);
			if (*pMin > height)
			{
				*pMin = height;
			}
			if (*pMax < height)
			{
				*pMax = height;
			}
		}
	}
}

UDWORD GetWidthOfMap(void)
{
	return mapWidth;
}



UDWORD GetHeightOfMap(void)
{

	return mapHeight;
}


// -----------------------------------------------------------------------------------
/* This will save out the visibility data */
BOOL	writeVisibilityData( STRING *pFileName )
{
UBYTE			*pFileData;		// Pointer to the necessary allocated memory
UBYTE			*pVisData;		// Pointer to the start of the map data
UDWORD			fileSize;		// How many bytes we need - depends on compression
FILE			*pFile;			// File pointer
VIS_SAVEHEADER	*psHeader;		// Pointer to the header part of the file
UDWORD			mapEntries;		// Effectively, how many tiles are there?
UDWORD			i;

	/* How many tiles do we write out data from? */
	mapEntries = mapWidth*mapHeight;

	/* Calculate memory required */
	fileSize = ( sizeof(struct _vis_save_header) + ( mapEntries*sizeof(UBYTE) ) );

	/* Try and allocate it - freed up in same function */
	pFileData = (UBYTE *)MALLOC(fileSize);

	/* Did we get it? */
	if(!pFileData)
	{
		/* Nope, so do one */	
		DBERROR(("Saving visibility data : Cannot get the memory! (%d)",fileSize));
		return(FALSE);
	} 

	/* We got the memory, so put the file header on the file */
	psHeader = (VIS_SAVEHEADER *)pFileData;
	psHeader->aFileType[0] = 'v';
	psHeader->aFileType[1] = 'i';
	psHeader->aFileType[2] = 's';
	psHeader->aFileType[3] = 'd';

	/* Wirte out the version number - unlikely to change for visibility data */
	psHeader->version = CURRENT_VERSION_NUM;

	/* Skip past the header to the raw data area */
	pVisData = pFileData + sizeof(struct _vis_save_header);

	 
	for(i=0; i<mapWidth*mapHeight; i++)
	{
			pVisData[i] = psMapTiles[i].tileVisBits;
	}

	/* Have a bash at opening the file to write */
	pFile = fopen(pFileName, "wb");
	if (!pFile)
	{
		DBERROR(("Saving visibility data : couldn't open file %s", pFileName));
		return(FALSE);
	}

	/* Now, try and write it out */
	if (fwrite(pFileData, 1, fileSize, pFile) != fileSize)
	{
		DBERROR(("Saving visibility data : write failed for %s", pFileName));
		return(FALSE);
	}

	/* Finally, try and close it */
	if (fclose(pFile) != 0)
	{
		DBERROR(("Saving visibility data : couldn't close %s", pFileName));
		return(FALSE);
	}

	/* And free up the memory we used */
	if (pFileData != NULL)
	{
		FREE(pFileData);
	}
	/* Everything is just fine! */
	return TRUE;
}

// -----------------------------------------------------------------------------------
/* This will read in the visibility data */
BOOL	readVisibilityData( UBYTE *pFileData, UDWORD fileSize )
{
UDWORD				expectedFileSize;
UDWORD				mapEntries;
VIS_SAVEHEADER		*psHeader;
UDWORD				i;
UBYTE				*pVisData;

	/* See if we've been given the right file type? */
	psHeader = (VIS_SAVEHEADER *)pFileData;
	if (psHeader->aFileType[0] != 'v' || psHeader->aFileType[1] != 'i' ||
		psHeader->aFileType[2] != 's' || psHeader->aFileType[3] != 'd')	{
		DBERROR(("Read visibility data : Weird file type found? Has header letters \
				  - %s %s %s %s", psHeader->aFileType[0],psHeader->aFileType[1],
								  psHeader->aFileType[2],psHeader->aFileType[3]));
		return FALSE;
	}

	/* How much data are we expecting? */
	mapEntries = (mapWidth*mapHeight);
	expectedFileSize = (sizeof(struct _vis_save_header) + 	(mapEntries*sizeof(UBYTE)) );

	/* Is that what we've been given? */
	if(fileSize!=expectedFileSize)
	{
		/* No, so bomb out */
		DBERROR(("Read visibility data : Weird file size for %d by %d sized map?",
					mapWidth,mapHeight));
		return(FALSE);
	}
	
	/* Skip past the header gubbins - can check version number here too */	
	pVisData = (UBYTE*)pFileData + sizeof(struct _vis_save_header);

	/* For every tile... */
	for(i=0; i<mapWidth*mapHeight; i++)
	{
		/* Get the visibility data */
		psMapTiles[i].tileVisBits = pVisData[i];
	}

	/* Hopefully everything's just fine by now */
	return(TRUE);
}
// -----------------------------------------------------------------------------------


