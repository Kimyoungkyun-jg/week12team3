#pragma once

#include "Core/Types.h"
#include "Core/EngineString.h"

// FbxDebugScene.h
namespace fbxsdk
{
	class FbxScene;
	class FbxGlobalSettings;
	class FbxNode;
	class FbxObject;
	class FbxMesh;
	class FbxVector4;
	class FbxVector2;
	class FbxColor;
	class FbxGeometry;
	class FbxImplementation;
	class FbxSurfaceMaterial;
	class FbxProperty;
	class FbxTexture;
	class FbxString;
}

// Import가 끝난 FbxScene의 내용을 HTR_LOG로 찍는 디버그용 함수 모음.
// samples/ImportScene의 Display* 함수들을 엔진 로그에 맞게 옮긴 것이다. 렌더링 데이터 변환은 FFbxAssetImporter가 맡는다.
class FFbxDebugScene
{
public:
	static bool DumpFile(const FString& Path, FString& Error);
	static void DisplayVector4(const char* pHeader, fbxsdk::FbxVector4 pValue);
	static void DisplayVector3(const char* pHeader, fbxsdk::FbxVector4 pValue);
	static void DisplayVector2(const char* pHeader, fbxsdk::FbxVector2 pValue);
	static void DisplayColor(const char* pHeader, fbxsdk::FbxColor pValue);
	static void DisplayBool(const char* pHeader, bool pValue);
	static void DisplayDouble(const char* pHeader, double pValue);

	static void DisplayScene(fbxsdk::FbxScene* InScene);
	static void DisplayMetaData(fbxsdk::FbxScene* InScene);
	static void DisplayLightSettings(fbxsdk::FbxGlobalSettings* InSettings);
	static void DisplayTimeSettings(fbxsdk::FbxGlobalSettings* InSettings);
	static void DisplayAxisAndUnit(fbxsdk::FbxGlobalSettings* InSettings);
	static void DisplayHierarchy(fbxsdk::FbxScene* InScene);
	static void DisplayHierarchy(fbxsdk::FbxNode* pNode, int pDepth);
	static void DisplayContent(fbxsdk::FbxScene* pScene);
	static void DisplayContent(fbxsdk::FbxNode* pNode);
	static void DisplayMetaDataConnections(fbxsdk::FbxObject* pObject);
	static void DisplayNodeTransform(fbxsdk::FbxNode* pNode);
	static void DisplayPivotsAndLimits(fbxsdk::FbxNode* pNode);
	static void DisplayTransformPropagation(fbxsdk::FbxNode* pNode);
	static void DisplayGeometricTransform(fbxsdk::FbxNode* pNode);
	static void DisplayMarker(fbxsdk::FbxNode* pNode);
	static void DisplaySkeleton(fbxsdk::FbxNode* pNode);
	static void DisplayMesh(fbxsdk::FbxNode* pNode);
	static void DisplayPolygonSummary(fbxsdk::FbxMesh* pMesh);
	static void DisplayControlPoints(fbxsdk::FbxMesh* pMesh);
	static void DisplayPolygons(fbxsdk::FbxMesh* pMesh);
	static void DisplayMaterialMapping(fbxsdk::FbxMesh* pMesh);
	static void DisplayMaterial(fbxsdk::FbxGeometry* pGeometry);
	static const fbxsdk::FbxImplementation* LookForImplementation(fbxsdk::FbxSurfaceMaterial* pMaterial);
	static void DisplayTexture(fbxsdk::FbxGeometry* pGeometry);
	static void FindAndDisplayTextureInfoByProperty(fbxsdk::FbxProperty& pProperty, bool& pDisplayHeader, int pMaterialIndex);
	static void DisplayTextureInfo(fbxsdk::FbxTexture* pTexture, int pBlendMode);
	static void DisplayMaterialConnections(fbxsdk::FbxMesh* pMesh);
	static void DisplayMaterialTextureConnections(fbxsdk::FbxSurfaceMaterial* pMaterial, int pMatId, int pElementIndex);
	static void DisplayTextureNames(fbxsdk::FbxProperty& pProperty, fbxsdk::FbxString& pConnectionString);
	// 씬의 모든 파일 텍스처에 대해 절대/상대 경로와 실제 파일 존재 여부를 찍는다. 상대 경로는 fbx 파일 폴더 기준이다.
	static void DisplayTexturePaths(fbxsdk::FbxScene* InScene, const FString& FbxPath);
};
