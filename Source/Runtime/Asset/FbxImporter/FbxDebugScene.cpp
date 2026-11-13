#include "EnginePCH.h"
#include "FbxDebugScene.h"

//아래 define은 안 붙여줘도 되지만 fbxsdk 클래스를 들어갔다가 나오면 모호하다고 빨간 줄 뜬다.
//아래 define을 0으로 바꿨다가 1로 다시 바꾸면 모호해지지 않는다.(1로 두도록 하자. 0이면 안된다)
#define FBXSDK_NAMESPACE_USING 1
#include <fbxsdk.h>

#include <map>

#include "Core/EngineLog.h"
#include "Asset/FbxImporter/FbxAssetBake.h"
#include "Asset/FbxImporter/FbxPath.h"

bool FFbxDebugScene::DumpFile(const FString& Path, FString& Error)
{
	FString SourcePath = Path;
	if (std::filesystem::path(Path).extension() == ".fbxbin")
	{
		FFbxAssetData Data;
		if (!FFbxAssetBake::ReadHeader(Path, Data, Error)) return false;
		SourcePath = FbxPath::ToString(FbxPath::SourceFile(Data.ActualSourcePath));
	}
	auto* Manager = FbxManager::Create();
	if (!Manager) { Error = "Cannot create FBX manager"; return false; }
	Manager->SetIOSettings(FbxIOSettings::Create(Manager, IOSROOT));
	auto* Importer = FbxImporter::Create(Manager, "DebugReport");
	if (!Importer->Initialize(SourcePath.c_str(), -1, Manager->GetIOSettings()))
	{ Error = Importer->GetStatus().GetErrorString(); Manager->Destroy(); return false; }
	auto* Scene = FbxScene::Create(Manager, "DebugReport");
	if (!Importer->Import(Scene))
	{ Error = Importer->GetStatus().GetErrorString(); Manager->Destroy(); return false; }
	DisplayScene(Scene);
	DisplayTexturePaths(Scene, SourcePath);
	Manager->Destroy();
	return true;
}

void FFbxDebugScene::DisplayVector4(const char* pHeader, fbxsdk::FbxVector4 pValue)
{
	FbxString lFloatValue1 = static_cast<float>(pValue[0]);
	FbxString lFloatValue2 = static_cast<float>(pValue[1]);
	FbxString lFloatValue3 = static_cast<float>(pValue[2]);
	FbxString lFloatValue4 = static_cast<float>(pValue[3]);

	lFloatValue1 = pValue[0] <= -HUGE_VAL ? "-INFINITY" : lFloatValue1.Buffer();
	lFloatValue1 = pValue[0] >= HUGE_VAL ? "INFINITY" : lFloatValue1.Buffer();
	lFloatValue2 = pValue[1] <= -HUGE_VAL ? "-INFINITY" : lFloatValue2.Buffer();
	lFloatValue2 = pValue[1] >= HUGE_VAL ? "INFINITY" : lFloatValue2.Buffer();
	lFloatValue3 = pValue[2] <= -HUGE_VAL ? "-INFINITY" : lFloatValue3.Buffer();
	lFloatValue3 = pValue[2] >= HUGE_VAL ? "INFINITY" : lFloatValue3.Buffer();
	lFloatValue4 = pValue[3] <= -HUGE_VAL ? "-INFINITY" : lFloatValue4.Buffer();
	lFloatValue4 = pValue[3] >= HUGE_VAL ? "INFINITY" : lFloatValue4.Buffer();

	HTR_LOG(Info, "{}{}, {}, {}, {}", pHeader, lFloatValue1.Buffer(), lFloatValue2.Buffer(), lFloatValue3.Buffer(), lFloatValue4.Buffer());
}

void FFbxDebugScene::DisplayVector3(const char* pHeader, fbxsdk::FbxVector4 pValue)
{
	FbxString lFloatValue1 = static_cast<float>(pValue[0]);
	FbxString lFloatValue2 = static_cast<float>(pValue[1]);
	FbxString lFloatValue3 = static_cast<float>(pValue[2]);

	lFloatValue1 = pValue[0] <= -HUGE_VAL ? "-INFINITY" : lFloatValue1.Buffer();
	lFloatValue1 = pValue[0] >= HUGE_VAL ? "INFINITY" : lFloatValue1.Buffer();
	lFloatValue2 = pValue[1] <= -HUGE_VAL ? "-INFINITY" : lFloatValue2.Buffer();
	lFloatValue2 = pValue[1] >= HUGE_VAL ? "INFINITY" : lFloatValue2.Buffer();
	lFloatValue3 = pValue[2] <= -HUGE_VAL ? "-INFINITY" : lFloatValue3.Buffer();
	lFloatValue3 = pValue[2] >= HUGE_VAL ? "INFINITY" : lFloatValue3.Buffer();

	HTR_LOG(Info, "{}{}, {}, {}", pHeader, lFloatValue1.Buffer(), lFloatValue2.Buffer(), lFloatValue3.Buffer());
}

void FFbxDebugScene::DisplayVector2(const char* pHeader, fbxsdk::FbxVector2 pValue)
{
	FbxString lFloatValue1 = static_cast<float>(pValue[0]);
	FbxString lFloatValue2 = static_cast<float>(pValue[1]);

	lFloatValue1 = pValue[0] <= -HUGE_VAL ? "-INFINITY" : lFloatValue1.Buffer();
	lFloatValue1 = pValue[0] >= HUGE_VAL ? "INFINITY" : lFloatValue1.Buffer();
	lFloatValue2 = pValue[1] <= -HUGE_VAL ? "-INFINITY" : lFloatValue2.Buffer();
	lFloatValue2 = pValue[1] >= HUGE_VAL ? "INFINITY" : lFloatValue2.Buffer();

	HTR_LOG(Info, "{}{}, {}", pHeader, lFloatValue1.Buffer(), lFloatValue2.Buffer());
}

void FFbxDebugScene::DisplayColor(const char* pHeader, fbxsdk::FbxColor pValue)
{
	FbxString lString;

	lString = pHeader;
	lString += (float)pValue.mRed;

	lString += " (red), ";
	lString += (float)pValue.mGreen;

	lString += " (green), ";
	lString += (float)pValue.mBlue;

	lString += " (blue)";

	HTR_LOG(Info, "{}", lString.Buffer());
}

void FFbxDebugScene::DisplayBool(const char* pHeader, bool pValue)
{
	FbxString lString;

	lString = pHeader;
	lString += pValue ? "true" : "false";
	HTR_LOG(Info, "{}", lString.Buffer());
}

void FFbxDebugScene::DisplayDouble(const char* pHeader, double pValue)
{
	FbxString lString;
	FbxString lFloatValue = static_cast<float>(pValue);

	lFloatValue = pValue <= -HUGE_VAL ? "-INFINITY" : lFloatValue.Buffer();
	lFloatValue = pValue >= HUGE_VAL ? "INFINITY" : lFloatValue.Buffer();

	lString = pHeader;
	lString += lFloatValue;
	HTR_LOG(Info, "{}", lString.Buffer());
}

void FFbxDebugScene::DisplayScene(fbxsdk::FbxScene* InScene)
{
	//MetaData
	HTR_LOG(Info, "--------------------Meta-Data--------------------");
	DisplayMetaData(InScene);

	FbxGlobalSettings& GSetting = InScene->GetGlobalSettings();

	//Light Color
	HTR_LOG(Info, "--------------------Light Setting--------------------");
	DisplayLightSettings(&GSetting);

	//Camera
	HTR_LOG(Info, "--------------------Camera--------------------");
	HTR_LOG(Info, "Default Camera : {}", GSetting.GetDefaultCamera().Buffer());

	//Axis & Unit
	HTR_LOG(Info, "--------------------Axis & Unit--------------------");
	DisplayAxisAndUnit(&GSetting);

	//Global Time Setting
	HTR_LOG(Info, "--------------------Time Setting--------------------");
	DisplayTimeSettings(&GSetting);

	//Hierarchy
	HTR_LOG(Info, "--------------------Hierarchy--------------------");
	DisplayHierarchy(InScene);

	//Content
	HTR_LOG(Info, "--------------------Content--------------------");
	DisplayContent(InScene);
}

void FFbxDebugScene::DisplayMetaData(fbxsdk::FbxScene* InScene)
{
	FbxDocumentInfo* sceneInfo = InScene->GetSceneInfo();

	if (sceneInfo)
	{
		HTR_LOG(Info, "    Title: {}", sceneInfo->mTitle.Buffer());
		HTR_LOG(Info, "    Subject: {}", sceneInfo->mSubject.Buffer());
		HTR_LOG(Info, "    Author: {}", sceneInfo->mAuthor.Buffer());
		HTR_LOG(Info, "    Keywords: {}", sceneInfo->mKeywords.Buffer());
		HTR_LOG(Info, "    Revision: {}", sceneInfo->mRevision.Buffer());
		HTR_LOG(Info, "    Comment: {}", sceneInfo->mComment.Buffer());

		FbxThumbnail* thumbnail = sceneInfo->GetSceneThumbnail();
		if (thumbnail)
		{
			HTR_LOG(Info, "    Thumbnail:");

			switch (thumbnail->GetDataFormat())
			{
				case FbxThumbnail::eRGB_24:
					HTR_LOG(Info, "        Format: RGB");
					break;
				case FbxThumbnail::eRGBA_32:
					HTR_LOG(Info, "        Format: RGBA");
					break;
			}

			switch (thumbnail->GetSize())
			{
				default:
					break;
				case FbxThumbnail::eNotSet:
					HTR_LOG(Info, "        Size: no dimensions specified ({} bytes)", thumbnail->GetSizeInBytes());
					break;
				case FbxThumbnail::e64x64:
					HTR_LOG(Info, "        Size: 64 x 64 pixels ({} bytes)", thumbnail->GetSizeInBytes());
					break;
				case FbxThumbnail::e128x128:
					HTR_LOG(Info, "        Size: 128 x 128 pixels ({} bytes)", thumbnail->GetSizeInBytes());
			}
		}
	}
}

void FFbxDebugScene::DisplayLightSettings(fbxsdk::FbxGlobalSettings* InSettings)
{
	FbxColor pValue = InSettings->GetAmbientColor();

	DisplayColor("Ambient Color : ", pValue);
	/*FbxString lString;

	lString = "Ambient Color : ";
	lString += (float)pValue.mRed;

	lString += " (red), ";
	lString += (float)pValue.mGreen;

	lString += " (green), ";
	lString += (float)pValue.mBlue;

	lString += " (blue)";

	HTR_LOG(Info, "{}", lString.Buffer());*/
}

void FFbxDebugScene::DisplayTimeSettings(fbxsdk::FbxGlobalSettings* InSettings)
{
	HTR_LOG(Info, "Time Node : {}", FbxGetTimeModeName(InSettings->GetTimeMode()));
	FbxTimeSpan lTs;
	FbxTime     lStart, lEnd;
	char lTimeString[256];
	InSettings->GetTimelineDefaultTimeSpan(lTs);
	lStart = lTs.GetStart();
	lEnd = lTs.GetStop();
	HTR_LOG(Info, "Timeline default timespan: ");
	HTR_LOG(Info, "     Start: {}", lStart.GetTimeString(lTimeString, FbxUShort(256)));
	HTR_LOG(Info, "     Stop : {}", lEnd.GetTimeString(lTimeString, FbxUShort(256)));
}

void FFbxDebugScene::DisplayAxisAndUnit(fbxsdk::FbxGlobalSettings* InSettings)
{
	FbxAxisSystem AxisSystem = InSettings->GetAxisSystem();

	int UpSign = 0;
	int FrontSign = 0;
	const FbxAxisSystem::EUpVector UpVector = AxisSystem.GetUpVector(UpSign);
	const FbxAxisSystem::EFrontVector FrontVector = AxisSystem.GetFrontVector(FrontSign);

	const char* AxisNames[] = { "?", "X", "Y", "Z" };
	HTR_LOG(Info, "Up Axis : {}{}", UpSign < 0 ? "-" : "+", AxisNames[UpVector]);

	// Front는 Up을 뺀 나머지 두 축 중 하나다. ParityEven이면 남은 축 중 앞 글자, ParityOdd면 뒤 글자다.
	const bool bEven = FrontVector == FbxAxisSystem::eParityEven;
	const char* FrontAxis = "?";
	switch (UpVector)
	{
		case FbxAxisSystem::eXAxis: FrontAxis = bEven ? "Y" : "Z"; break;
		case FbxAxisSystem::eYAxis: FrontAxis = bEven ? "X" : "Z"; break;
		case FbxAxisSystem::eZAxis: FrontAxis = bEven ? "X" : "Y"; break;
	}
	HTR_LOG(Info, "Front Axis : {}{} ({})", FrontSign < 0 ? "-" : "+", FrontAxis, bEven ? "ParityEven" : "ParityOdd");
	HTR_LOG(Info, "Coord System : {}", AxisSystem.GetCoorSystem() == FbxAxisSystem::eRightHanded ? "Right Handed" : "Left Handed");

	// 같은 축 설정을 쓰는 프리셋이 여러 개일 수 있어서(Maya Y-Up = OpenGL 등) 일치하는 것을 모두 찍는다
	struct FAxisPreset
	{
		const char* Name;
		const FbxAxisSystem& Axis;
	};
	const FAxisPreset Presets[] =
	{
		{ "Maya Y-Up", FbxAxisSystem::MayaYUp },
		{ "Maya Z-Up", FbxAxisSystem::MayaZUp },
		{ "3ds Max", FbxAxisSystem::Max },
		{ "MotionBuilder", FbxAxisSystem::Motionbuilder },
		{ "OpenGL", FbxAxisSystem::OpenGL },
		{ "DirectX", FbxAxisSystem::DirectX },
		{ "Lightwave", FbxAxisSystem::Lightwave },
	};

	FString MatchedPresets;
	for (const FAxisPreset& Preset : Presets)
	{
		if (AxisSystem == Preset.Axis)
		{
			MatchedPresets += MatchedPresets.empty() ? Preset.Name : FString(", ") + Preset.Name;
		}
	}
	HTR_LOG(Info, "Matched Preset : {}", MatchedPresets.empty() ? "None" : MatchedPresets);

	// 0 = X, 1 = Y, 2 = Z. 파일을 처음 만든 툴의 Up 축이다(Convert 이후에도 남는다).
	const int OriginalUpAxis = InSettings->GetOriginalUpAxis();
	HTR_LOG(Info, "Original Up Axis : {}", (OriginalUpAxis >= 0 && OriginalUpAxis <= 2) ? AxisNames[OriginalUpAxis + 1] : "Unknown");

	// ScaleFactor는 1 unit이 몇 cm인지를 뜻한다 (cm = 1, m = 100)
	const FbxSystemUnit SystemUnit = InSettings->GetSystemUnit();
	HTR_LOG(Info, "System Unit : {}", SystemUnit.GetScaleFactorAsString(false).Buffer());
	DisplayDouble("    Scale Factor (cm per unit): ", SystemUnit.GetScaleFactor());
	DisplayDouble("    Multiplier: ", SystemUnit.GetMultiplier());

	const FbxSystemUnit OriginalUnit = InSettings->GetOriginalSystemUnit();
	HTR_LOG(Info, "Original System Unit : {}", OriginalUnit.GetScaleFactorAsString(false).Buffer());
	DisplayDouble("    Scale Factor (cm per unit): ", OriginalUnit.GetScaleFactor());
}

void FFbxDebugScene::DisplayHierarchy(fbxsdk::FbxScene* InScene)
{
	int i;
	FbxNode* lRootNode = InScene->GetRootNode();
		
	for (i = 0; i < lRootNode->GetChildCount(); i++)
	{
		DisplayHierarchy(lRootNode->GetChild(i), 0);
	}
}

void FFbxDebugScene::DisplayHierarchy(fbxsdk::FbxNode* pNode, int pDepth)
{
	FbxString lString;
	int i;

	for (i = 0; i < pDepth; i++)
	{
		lString += "  ";
	}

	lString += pNode->GetName();
	HTR_LOG(Info, "{}", lString.Buffer());

	for (i = 0; i < pNode->GetChildCount(); i++)
	{
		DisplayHierarchy(pNode->GetChild(i), pDepth + 1);
	}
}

void FFbxDebugScene::DisplayContent(fbxsdk::FbxScene* pScene)
{
	int i;
	FbxNode* lNode = pScene->GetRootNode();

	if (lNode)
	{
		for (i = 0; i < lNode->GetChildCount(); i++)
		{
			DisplayContent(lNode->GetChild(i));
		}
	}
}

void FFbxDebugScene::DisplayContent(fbxsdk::FbxNode* pNode)
{
	if (pNode == nullptr) return;

	if (pNode->GetNodeAttribute() == nullptr)
	{
		HTR_LOG(Info, "NULL Node Attribute");
	}
	else
	{
		FbxNodeAttribute::EType AttributeType;
		AttributeType = pNode->GetNodeAttribute()->GetAttributeType();

		switch (AttributeType)
		{
			case fbxsdk::FbxNodeAttribute::eUnknown:
				break;
			case fbxsdk::FbxNodeAttribute::eNull:
				break;
			case fbxsdk::FbxNodeAttribute::eMarker:
				DisplayMarker(pNode);
				break;
			case fbxsdk::FbxNodeAttribute::eSkeleton:
				DisplaySkeleton(pNode);
				break;
			case fbxsdk::FbxNodeAttribute::eMesh:
				DisplayMesh(pNode);
				break;
			case fbxsdk::FbxNodeAttribute::eNurbs:
				break;
			case fbxsdk::FbxNodeAttribute::ePatch:
				break;
			case fbxsdk::FbxNodeAttribute::eCamera:
				break;
			case fbxsdk::FbxNodeAttribute::eCameraStereo:
				break;
			case fbxsdk::FbxNodeAttribute::eCameraSwitcher:
				break;
			case fbxsdk::FbxNodeAttribute::eLight:
				break;
			case fbxsdk::FbxNodeAttribute::eOpticalReference:
				break;
			case fbxsdk::FbxNodeAttribute::eOpticalMarker:
				break;
			case fbxsdk::FbxNodeAttribute::eNurbsCurve:
				break;
			case fbxsdk::FbxNodeAttribute::eTrimNurbsSurface:
				break;
			case fbxsdk::FbxNodeAttribute::eBoundary:
				break;
			case fbxsdk::FbxNodeAttribute::eNurbsSurface:
				break;
			case fbxsdk::FbxNodeAttribute::eShape:
				break;
			case fbxsdk::FbxNodeAttribute::eLODGroup:
				break;
			case fbxsdk::FbxNodeAttribute::eSubDiv:
				break;
			case fbxsdk::FbxNodeAttribute::eCachedEffect:
				break;
			case fbxsdk::FbxNodeAttribute::eLine:
				break;
			default:
				break;
		}
	}

	// Attribute 종류와 상관없이 노드 자체가 가진 변환 정보
	DisplayNodeTransform(pNode);
	DisplayPivotsAndLimits(pNode);
	DisplayTransformPropagation(pNode);
	DisplayGeometricTransform(pNode);
}

void FFbxDebugScene::DisplayMetaDataConnections(fbxsdk::FbxObject* pObject)
{
	int nbMetaData = pObject->GetSrcObjectCount<FbxObjectMetaData>();

	if (nbMetaData > 0)
	{
		HTR_LOG(Info, "    MetaData connections ");
	}

	for (int i = 0; i < nbMetaData; i++)
	{
		FbxObjectMetaData* metaData = pObject->GetSrcObject<FbxObjectMetaData>(i);
		HTR_LOG(Info, "        Name: {}", metaData->GetName());
	}
}

void FFbxDebugScene::DisplayNodeTransform(fbxsdk::FbxNode* pNode)
{
	HTR_LOG(Info, "    Node Transform");

	// 파일에 저장된 원래 값(Lcl). 피벗, Pre/Post-Rotation은 포함하지 않는다.
	DisplayVector3("        Lcl Translation: ", FbxVector4(pNode->LclTranslation.Get()));
	DisplayVector3("        Lcl Rotation: ", FbxVector4(pNode->LclRotation.Get()));
	DisplayVector3("        Lcl Scaling: ", FbxVector4(pNode->LclScaling.Get()));

	// Evaluate는 피벗, Pre/Post-Rotation까지 합친 결과다. Geometric Transform은 포함하지 않으니 메시에 쓸 때 따로 곱해야 한다.
	const FbxAMatrix LocalMatrix = pNode->EvaluateLocalTransform();
	DisplayVector3("        Evaluated Local T: ", LocalMatrix.GetT());
	DisplayVector3("        Evaluated Local R: ", LocalMatrix.GetR());
	DisplayVector3("        Evaluated Local S: ", LocalMatrix.GetS());

	const FbxAMatrix GlobalMatrix = pNode->EvaluateGlobalTransform();
	DisplayVector3("        Evaluated Global T: ", GlobalMatrix.GetT());
	DisplayVector3("        Evaluated Global R: ", GlobalMatrix.GetR());
	DisplayVector3("        Evaluated Global S: ", GlobalMatrix.GetS());
}

void FFbxDebugScene::DisplayPivotsAndLimits(fbxsdk::FbxNode* pNode)
{
	//Pivots
	HTR_LOG(Info, "    Pivot Information");

	FbxNode::EPivotState PivotState;
	pNode->GetPivotState(FbxNode::eSourcePivot, PivotState);
	HTR_LOG(Info, "        Pivot State: {}", PivotState == FbxNode::ePivotActive ? "Active" : "Reference");

	DisplayVector3("        Pre-Rotation: ", pNode->GetPreRotation(FbxNode::eSourcePivot));
	DisplayVector3("        Post-Rotation: ", pNode->GetPostRotation(FbxNode::eSourcePivot));
	DisplayVector3("        Rotation Pivot: ", pNode->GetRotationPivot(FbxNode::eSourcePivot));
	DisplayVector3("        Rotation Offset: ", pNode->GetRotationOffset(FbxNode::eSourcePivot));
	DisplayVector3("        Scaling Pivot: ", pNode->GetScalingPivot(FbxNode::eSourcePivot));
	DisplayVector3("        Scaling Offset: ", pNode->GetScalingOffset(FbxNode::eSourcePivot));

	//Limits : Translation, Rotation, Scaling이 같은 모양이라 축별 출력은 람다로 묶는다
	HTR_LOG(Info, "    Limits Information");

	auto DisplayLimits = [](const char* Name, bool bActive, const bool (&bMinActive)[3], const bool (&bMaxActive)[3],
		const FbxDouble3& MinValues, const FbxDouble3& MaxValues)
	{
		const char* AxisNames[] = { "X", "Y", "Z" };

		HTR_LOG(Info, "        {} limits: {}", Name, bActive ? "Active" : "Inactive");
		for (int Axis = 0; Axis < 3; ++Axis)
		{
			HTR_LOG(Info, "            {}", AxisNames[Axis]);
			HTR_LOG(Info, "                Min Limit: {}", bMinActive[Axis] ? "Active" : "Inactive");
			DisplayDouble("                Min Limit Value: ", MinValues[Axis]);
			HTR_LOG(Info, "                Max Limit: {}", bMaxActive[Axis] ? "Active" : "Inactive");
			DisplayDouble("                Max Limit Value: ", MaxValues[Axis]);
		}
	};

	DisplayLimits("Translation", pNode->TranslationActive.Get(),
		{ pNode->TranslationMinX.Get(), pNode->TranslationMinY.Get(), pNode->TranslationMinZ.Get() },
		{ pNode->TranslationMaxX.Get(), pNode->TranslationMaxY.Get(), pNode->TranslationMaxZ.Get() },
		pNode->TranslationMin.Get(), pNode->TranslationMax.Get());

	DisplayLimits("Rotation", pNode->RotationActive.Get(),
		{ pNode->RotationMinX.Get(), pNode->RotationMinY.Get(), pNode->RotationMinZ.Get() },
		{ pNode->RotationMaxX.Get(), pNode->RotationMaxY.Get(), pNode->RotationMaxZ.Get() },
		pNode->RotationMin.Get(), pNode->RotationMax.Get());

	DisplayLimits("Scaling", pNode->ScalingActive.Get(),
		{ pNode->ScalingMinX.Get(), pNode->ScalingMinY.Get(), pNode->ScalingMinZ.Get() },
		{ pNode->ScalingMaxX.Get(), pNode->ScalingMaxY.Get(), pNode->ScalingMaxZ.Get() },
		pNode->ScalingMin.Get(), pNode->ScalingMax.Get());
}

void FFbxDebugScene::DisplayTransformPropagation(fbxsdk::FbxNode* pNode)
{
	HTR_LOG(Info, "    Transformation Propagation");

	//Rotation Space (EFbxRotationOrder 순서와 같다)
	const char* RotationOrderNames[] = { "Euler XYZ", "Euler XZY", "Euler YZX", "Euler YXZ", "Euler ZXY", "Euler ZYX", "Spheric XYZ" };

	EFbxRotationOrder RotationOrder;
	pNode->GetRotationOrder(FbxNode::eSourcePivot, RotationOrder);
	HTR_LOG(Info, "        Rotation Space: {}", RotationOrderNames[RotationOrder]);

	//Rotation Space를 Limit에만 쓰고 나머지는 eEulerXYZ로 계산하는지
	HTR_LOG(Info, "        Use the Rotation Space for Limit specification only: {}",
		pNode->GetUseRotationSpaceForLimitOnly(FbxNode::eSourcePivot) ? "Yes" : "No");

	//Inherit Type : 부모의 회전(R)·스케일(S)을 자식이 어떤 순서로 물려받는지
	const char* InheritTypeNames[] = { "RrSs", "RSrs", "Rrs" };

	FbxTransform::EInheritType InheritType;
	pNode->GetTransformationInheritType(InheritType);
	HTR_LOG(Info, "        Transformation Inheritance: {}", InheritTypeNames[InheritType]);
}

void FFbxDebugScene::DisplayGeometricTransform(fbxsdk::FbxNode* pNode)
{
	// 이 노드의 Attribute(메시 등)에만 적용되고 자식에게는 물려주지 않는 변환
	HTR_LOG(Info, "    Geometric Transformations");

	DisplayVector3("        Translation: ", pNode->GetGeometricTranslation(FbxNode::eSourcePivot));
	DisplayVector3("        Rotation: ", pNode->GetGeometricRotation(FbxNode::eSourcePivot));
	DisplayVector3("        Scaling: ", pNode->GetGeometricScaling(FbxNode::eSourcePivot));
}

void FFbxDebugScene::DisplayMarker(fbxsdk::FbxNode* pNode)
{
	HTR_LOG(Info, "--------------------Marker--------------------");
	HTR_LOG(Info, "Marker Name : {}", pNode->GetName());

	FbxMarker* Marker = static_cast<FbxMarker*>(pNode->GetNodeAttribute());

	//MetaData
	DisplayMetaDataConnections(Marker);	

	//Marker Type
	FString String;

	switch (Marker->GetType())
	{
		case FbxMarker::eStandard:    String = "Standard";    break;
		case FbxMarker::eOptical:     String = "Optical";     break;
		case FbxMarker::eEffectorIK: String = "IK Effector"; break;
		case FbxMarker::eEffectorFK: String = "FK Effector"; break;
	}
	HTR_LOG(Info, "    Marker Type: {}", String);

	//Look
	switch (Marker->Look.Get())
	{
		default:
			break;
		case FbxMarker::eCube:         String = "Cube";        break;
		case FbxMarker::eHardCross:   String = "Hard Cross";  break;
		case FbxMarker::eLightCross:  String = "Light Cross"; break;
		case FbxMarker::eSphere:       String = "Sphere";      break;
	}
	HTR_LOG(Info, "    Marker Look: {}", String);

	//Size
	FbxString SizeString = FbxString(Marker->Size.Get());
	HTR_LOG(Info, "    Size: {}", SizeString.Buffer());

	// Color
	FbxDouble3 c = Marker->Color.Get();
	FbxColor color(c[0], c[1], c[2]);
	HTR_LOG(Info, "    Color: R {}, G {}, B {}, A {}", color.mRed, color.mGreen, color.mBlue, color.mAlpha);

	// IKPivot
	FbxVector4 IKPivot = Marker->IKPivot.Get();

	DisplayVector4("    IKPivot: ", IKPivot);	
}

void FFbxDebugScene::DisplaySkeleton(fbxsdk::FbxNode* pNode)
{
	FbxSkeleton* Skeleton = static_cast<FbxSkeleton*>(pNode->GetNodeAttribute());

	HTR_LOG(Info, "Skeleton Name: {}", pNode->GetName());

	DisplayMetaDataConnections(Skeleton);

	const char* lSkeletonTypes[] = { "Root", "Limb", "Limb Node", "Effector" };

	HTR_LOG(Info, "    Type: {}", lSkeletonTypes[Skeleton->GetSkeletonType()]);

	if (Skeleton->GetSkeletonType() == FbxSkeleton::eLimb)
	{
		double pValue = Skeleton->LimbLength.Get();
		FbxString lFloatValue = static_cast<float>(pValue);

		lFloatValue = pValue <= -HUGE_VAL ? "-INFINITY" : lFloatValue.Buffer();
		lFloatValue = pValue >= HUGE_VAL ? "INFINITY" : lFloatValue.Buffer();

		HTR_LOG(Info, "    Limb Length: {}", lFloatValue.Buffer());
	}
	else if (Skeleton->GetSkeletonType() == FbxSkeleton::eLimbNode)
	{
		double pValue = Skeleton->Size.Get();
		FbxString lFloatValue = static_cast<float>(pValue);

		lFloatValue = pValue <= -HUGE_VAL ? "-INFINITY" : lFloatValue.Buffer();
		lFloatValue = pValue >= HUGE_VAL ? "INFINITY" : lFloatValue.Buffer();

		HTR_LOG(Info, "    Limb Node Size: {}", lFloatValue.Buffer());
	}
	else if (Skeleton->GetSkeletonType() == FbxSkeleton::eRoot)
	{
		double pValue = Skeleton->Size.Get();
		FbxString lFloatValue = static_cast<float>(pValue);

		lFloatValue = pValue <= -HUGE_VAL ? "-INFINITY" : lFloatValue.Buffer();
		lFloatValue = pValue >= HUGE_VAL ? "INFINITY" : lFloatValue.Buffer();

		HTR_LOG(Info, "    Limb Root Size: {}", lFloatValue.Buffer());
	}

	FbxColor color = Skeleton->GetLimbNodeColor();
	HTR_LOG(Info, "    Color: R {}, G {}, B {}, A {}", color.mRed, color.mGreen, color.mBlue, color.mAlpha);
}

void FFbxDebugScene::DisplayMesh(fbxsdk::FbxNode* pNode)
{
	HTR_LOG(Info, "Mesh Name: {}", pNode->GetName());

	FbxMesh* Mesh = static_cast<FbxMesh*>(pNode->GetNodeAttribute());

	DisplayMetaDataConnections(Mesh);

	DisplayPolygonSummary(Mesh);

	DisplayControlPoints(Mesh);

	DisplayPolygons(Mesh);

	DisplayMaterialMapping(Mesh);

	DisplayMaterial(Mesh);

	DisplayTexture(Mesh);

	DisplayMaterialConnections(Mesh);
}

void FFbxDebugScene::DisplayPolygonSummary(fbxsdk::FbxMesh* pMesh)
{
	const int PolygonCount = pMesh->GetPolygonCount();

	HTR_LOG(Info, "    Polygon Summary");
	HTR_LOG(Info, "        Control Points: {}", pMesh->GetControlPointsCount());
	HTR_LOG(Info, "        Polygons: {}", PolygonCount);
	HTR_LOG(Info, "        Polygon Vertices: {}", pMesh->GetPolygonVertexCount());
	HTR_LOG(Info, "        Is Triangle Mesh: {}", pMesh->IsTriangleMesh() ? "true" : "false");

	// 폴리곤 크기(꼭짓점 수)별 개수. fan으로 나누면 n각형 하나가 삼각형 n-2개가 된다.
	std::map<int, int> PolygonSizeCounts;
	int TriangleCount = 0;
	for (int i = 0; i < PolygonCount; i++)
	{
		const int PolygonSize = pMesh->GetPolygonSize(i);
		++PolygonSizeCounts[PolygonSize];
		TriangleCount += PolygonSize >= 3 ? PolygonSize - 2 : 0;
	}

	for (const auto& [PolygonSize, Count] : PolygonSizeCounts)
	{
		HTR_LOG(Info, "        {}-gon: {}", PolygonSize, Count);
	}
	HTR_LOG(Info, "        Triangles after triangulation: {}", TriangleCount);

	// 레이어 요소마다 몇 개가 있고 어떤 Mapping/Reference로 저장됐는지
	const char* MappingTypes[] = { "None", "By Control Point", "By Polygon Vertex", "By Polygon", "By Edge", "All Same" };
	const char* ReferenceModes[] = { "Direct", "Index", "Index to Direct" };

	auto DisplayLayerElement = [&](const char* Name, int Index, FbxLayerElement* Element)
	{
		if (Element == nullptr) return;

		HTR_LOG(Info, "        {} {} \"{}\": Mapping {}, Reference {}", Name, Index, Element->GetName(),
			MappingTypes[Element->GetMappingMode()], ReferenceModes[Element->GetReferenceMode()]);
	};

	HTR_LOG(Info, "    Layer Elements");
	for (int i = 0; i < pMesh->GetElementNormalCount(); i++)
	{
		DisplayLayerElement("Normal", i, pMesh->GetElementNormal(i));
	}
	for (int i = 0; i < pMesh->GetElementTangentCount(); i++)
	{
		DisplayLayerElement("Tangent", i, pMesh->GetElementTangent(i));
	}
	for (int i = 0; i < pMesh->GetElementBinormalCount(); i++)
	{
		DisplayLayerElement("Binormal", i, pMesh->GetElementBinormal(i));
	}
	for (int i = 0; i < pMesh->GetElementUVCount(); i++)
	{
		DisplayLayerElement("UV", i, pMesh->GetElementUV(i));
	}
	for (int i = 0; i < pMesh->GetElementVertexColorCount(); i++)
	{
		DisplayLayerElement("Vertex Color", i, pMesh->GetElementVertexColor(i));
	}
	for (int i = 0; i < pMesh->GetElementSmoothingCount(); i++)
	{
		DisplayLayerElement("Smoothing", i, pMesh->GetElementSmoothing(i));
	}
	for (int i = 0; i < pMesh->GetElementMaterialCount(); i++)
	{
		DisplayLayerElement("Material", i, pMesh->GetElementMaterial(i));
	}
}

void FFbxDebugScene::DisplayControlPoints(fbxsdk::FbxMesh* pMesh)
{
	int lControlPointsCount = pMesh->GetControlPointsCount();
	FbxVector4* lControlPoints = pMesh->GetControlPoints();

	HTR_LOG(Info, "    Control Points");

	for (int i = 0; i < lControlPointsCount; i++)
	{
		HTR_LOG(Info, "        Control Point {}", i);

		DisplayVector4("            Coordinates: ", lControlPoints[i]);

		for (int j = 0; j < pMesh->GetElementNormalCount(); j++)
		{
			FbxGeometryElementNormal* leNormals = pMesh->GetElementNormal(j);
			if (leNormals->GetMappingMode() == FbxGeometryElement::eByControlPoint)
			{
				char header[100];
				FBXSDK_sprintf(header, 100, "            Normal Vector: ");
				if (leNormals->GetReferenceMode() == FbxGeometryElement::eDirect)
				{
					DisplayVector4(header, leNormals->GetDirectArray().GetAt(i));
				}
			}
		}
	}
}

void FFbxDebugScene::DisplayPolygons(fbxsdk::FbxMesh* pMesh)
{
	int lPolygonCount = pMesh->GetPolygonCount();
	FbxVector4* lControlPoints = pMesh->GetControlPoints();

	char header[100];

	HTR_LOG(Info, "    Polygons");

	int vertexId = 0;

	for (int i = 0; i < lPolygonCount; i++)
	{
		HTR_LOG(Info, "        Polygon {}", i);

		for (int l = 0; l < pMesh->GetElementPolygonGroupCount(); l++)
		{
			FbxGeometryElementPolygonGroup* lePolgrp = pMesh->GetElementPolygonGroup(l);
			switch (lePolgrp->GetMappingMode())
			{
				case FbxGeometryElement::eByPolygon:
					if (lePolgrp->GetReferenceMode() == FbxGeometryElement::eIndex)
					{
						FBXSDK_sprintf(header, 100, "        Assigned to group: ");
						int polyGroupId = lePolgrp->GetIndexArray().GetAt(i);
						HTR_LOG(Info, "{}{}", header, polyGroupId);
						break;
					}
				default:
					// any other mapping modes don't make sense
					HTR_LOG(Info, "        \"unsupported group assignment\"");
					break;
			}
		}

		int lPolygonSize = pMesh->GetPolygonSize(i);

		for (int j = 0; j < lPolygonSize; j++)
		{
			int lControlPointIndex = pMesh->GetPolygonVertex(i, j);
			if (lControlPointIndex < 0)
			{
				HTR_LOG(Error, "            Coordinates: Invalid index found!");
				continue;
			}
			else
			{
				DisplayVector4("            Coordinates: ", lControlPoints[lControlPointIndex]);
			}

			for (int l = 0; l < pMesh->GetElementVertexColorCount(); l++)
			{
				FbxGeometryElementVertexColor* leVtxc = pMesh->GetElementVertexColor(l);
				FBXSDK_sprintf(header, 100, "            Color vertex: ");

				switch (leVtxc->GetMappingMode())
				{
					case FbxGeometryElement::eByControlPoint:
						switch (leVtxc->GetReferenceMode())
						{
							case FbxGeometryElement::eDirect:
								DisplayColor(header, leVtxc->GetDirectArray().GetAt(lControlPointIndex));
								break;
							case FbxGeometryElement::eIndexToDirect:
							{
								int id = leVtxc->GetIndexArray().GetAt(lControlPointIndex);
								DisplayColor(header, leVtxc->GetDirectArray().GetAt(id));
							}
							break;
							default:
								break; // other reference modes not shown here!
						}
						break;

					case FbxGeometryElement::eByPolygonVertex:
					{
						switch (leVtxc->GetReferenceMode())
						{
							case FbxGeometryElement::eDirect:
								DisplayColor(header, leVtxc->GetDirectArray().GetAt(vertexId));
								break;
							case FbxGeometryElement::eIndexToDirect:
							{
								int id = leVtxc->GetIndexArray().GetAt(vertexId);
								DisplayColor(header, leVtxc->GetDirectArray().GetAt(id));
							}
							break;
							default:
								break; // other reference modes not shown here!
						}
					}
					break;

					case FbxGeometryElement::eByPolygon: // doesn't make much sense for UVs
					case FbxGeometryElement::eAllSame:   // doesn't make much sense for UVs
					case FbxGeometryElement::eNone:       // doesn't make much sense for UVs
						break;
					default:
						break;
				}
			}
			for (int l = 0; l < pMesh->GetElementUVCount(); ++l)
			{
				FbxGeometryElementUV* leUV = pMesh->GetElementUV(l);
				FBXSDK_sprintf(header, 100, "            Texture UV: ");

				switch (leUV->GetMappingMode())
				{
					default:
						break;
					case FbxGeometryElement::eByControlPoint:
						switch (leUV->GetReferenceMode())
						{
							case FbxGeometryElement::eDirect:
								DisplayVector2(header, leUV->GetDirectArray().GetAt(lControlPointIndex));
								break;
							case FbxGeometryElement::eIndexToDirect:
							{
								int id = leUV->GetIndexArray().GetAt(lControlPointIndex);
								DisplayVector2(header, leUV->GetDirectArray().GetAt(id));
							}
							break;
							default:
								break; // other reference modes not shown here!
						}
						break;

					case FbxGeometryElement::eByPolygonVertex:
					{
						int lTextureUVIndex = pMesh->GetTextureUVIndex(i, j);
						switch (leUV->GetReferenceMode())
						{
							case FbxGeometryElement::eDirect:
							case FbxGeometryElement::eIndexToDirect:
							{
								DisplayVector2(header, leUV->GetDirectArray().GetAt(lTextureUVIndex));
							}
							break;
							default:
								break; // other reference modes not shown here!
						}
					}
					break;

					case FbxGeometryElement::eByPolygon: // doesn't make much sense for UVs
					case FbxGeometryElement::eAllSame:   // doesn't make much sense for UVs
					case FbxGeometryElement::eNone:       // doesn't make much sense for UVs
						break;
				}
			}
			for (int l = 0; l < pMesh->GetElementNormalCount(); ++l)
			{
				FbxGeometryElementNormal* leNormal = pMesh->GetElementNormal(l);
				FBXSDK_sprintf(header, 100, "            Normal: ");

				if (leNormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (leNormal->GetReferenceMode())
					{
						case FbxGeometryElement::eDirect:
							DisplayVector3(header, leNormal->GetDirectArray().GetAt(vertexId));
							break;
						case FbxGeometryElement::eIndexToDirect:
						{
							int id = leNormal->GetIndexArray().GetAt(vertexId);
							DisplayVector3(header, leNormal->GetDirectArray().GetAt(id));
						}
						break;
						default:
							break; // other reference modes not shown here!
					}
				}

			}
			for (int l = 0; l < pMesh->GetElementTangentCount(); ++l)
			{
				FbxGeometryElementTangent* leTangent = pMesh->GetElementTangent(l);
				FBXSDK_sprintf(header, 100, "            Tangent: ");

				if (leTangent->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (leTangent->GetReferenceMode())
					{
						case FbxGeometryElement::eDirect:
							DisplayVector3(header, leTangent->GetDirectArray().GetAt(vertexId));
							break;
						case FbxGeometryElement::eIndexToDirect:
						{
							int id = leTangent->GetIndexArray().GetAt(vertexId);
							DisplayVector3(header, leTangent->GetDirectArray().GetAt(id));
						}
						break;
						default:
							break; // other reference modes not shown here!
					}
				}

			}
			for (int l = 0; l < pMesh->GetElementBinormalCount(); ++l)
			{

				FbxGeometryElementBinormal* leBinormal = pMesh->GetElementBinormal(l);

				FBXSDK_sprintf(header, 100, "            Binormal: ");
				if (leBinormal->GetMappingMode() == FbxGeometryElement::eByPolygonVertex)
				{
					switch (leBinormal->GetReferenceMode())
					{
						case FbxGeometryElement::eDirect:
							DisplayVector3(header, leBinormal->GetDirectArray().GetAt(vertexId));
							break;
						case FbxGeometryElement::eIndexToDirect:
						{
							int id = leBinormal->GetIndexArray().GetAt(vertexId);
							DisplayVector3(header, leBinormal->GetDirectArray().GetAt(id));
						}
						break;
						default:
							break; // other reference modes not shown here!
					}
				}
			}
			vertexId++;
		} // for polygonSize
	} // for polygonCount


	//check visibility for the edges of the mesh
	for (int l = 0; l < pMesh->GetElementVisibilityCount(); ++l)
	{
		FbxGeometryElementVisibility* leVisibility = pMesh->GetElementVisibility(l);
		FBXSDK_sprintf(header, 100, "    Edge Visibility : ");
		HTR_LOG(Info, "{}", header);

		switch (leVisibility->GetMappingMode())
		{
			default:
				break;
				//should be eByEdge
			case FbxGeometryElement::eByEdge:
				//should be eDirect
				for (int j = 0; j != pMesh->GetMeshEdgeCount(); ++j)
				{
					HTR_LOG(Info, "        Edge {}", j);
					HTR_LOG(Info, "              Edge visibility: {}", leVisibility->GetDirectArray().GetAt(j) ? "true" : "false");					
				}

				break;
		}
	}
}

void FFbxDebugScene::DisplayMaterialMapping(fbxsdk::FbxMesh* pMesh)
{
	if (pMesh == nullptr) return;

	const char* lMappingTypes[] = { "None", "By Control Point", "By Polygon Vertex", "By Polygon", "By Edge", "All Same" };
	const char* lReferenceMode[] = { "Direct", "Index", "Index to Direct" };

	int lMtrlCount = 0;
	FbxNode* lNode = nullptr;
	lNode = pMesh->GetNode();

	if (lNode)
	{
		lMtrlCount = lNode->GetMaterialCount();
	}

	for (int l = 0; l < pMesh->GetElementMaterialCount(); l++)
	{
		FbxGeometryElementMaterial* leMat = pMesh->GetElementMaterial(l);
		if (leMat)
		{
			char header[100];
			FBXSDK_sprintf(header, 100, "    Material Element %d: ", l);
			HTR_LOG(Info, "{}", header);


			HTR_LOG(Info, "           Mapping: {}", lMappingTypes[leMat->GetMappingMode()]);
			HTR_LOG(Info, "           ReferenceMode: {}", lReferenceMode[leMat->GetReferenceMode()]);

			int lMaterialCount = 0;
			FbxString lString;

			if (leMat->GetReferenceMode() == FbxGeometryElement::eDirect ||
				leMat->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
			{
				lMaterialCount = lMtrlCount;
			}

			if (leMat->GetReferenceMode() == FbxGeometryElement::eIndex ||
				leMat->GetReferenceMode() == FbxGeometryElement::eIndexToDirect)
			{
				lString = "           Indices: ";

				int lIndexArrayCount = leMat->GetIndexArray().GetCount();
				for (int i = 0; i < lIndexArrayCount; i++)
				{
					lString += leMat->GetIndexArray().GetAt(i);

					if (i < lIndexArrayCount - 1)
					{
						lString += ", ";
					}
				}

				HTR_LOG(Info, "{}", lString.Buffer());
			}
		}
	}
}

void FFbxDebugScene::DisplayMaterial(fbxsdk::FbxGeometry* pGeometry)
{
	int lMaterialCount = 0;
	FbxNode* lNode = NULL;
	if (pGeometry)
	{
		lNode = pGeometry->GetNode();
		if (lNode)
			lMaterialCount = lNode->GetMaterialCount();
	}

	if (lMaterialCount > 0)
	{
		FbxPropertyT<FbxDouble3> lKFbxDouble3;
		FbxPropertyT<FbxDouble> lKFbxDouble1;
		FbxColor theColor;

		for (int lCount = 0; lCount < lMaterialCount; lCount++)
		{
			HTR_LOG(Info, "        Material {}", lCount);

			FbxSurfaceMaterial* lMaterial = lNode->GetMaterial(lCount);

			HTR_LOG(Info, "            Name: \"{}\"", lMaterial->GetName());

			//Get the implementation to see if it's a hardware shader.
			const FbxImplementation* lImplementation = LookForImplementation(lMaterial);
			if (lImplementation)
			{
				//Now we have a hardware shader, let's read it
				HTR_LOG(Info, "            Language: ", lImplementation->Language.Get().Buffer());
				HTR_LOG(Info, "            LanguageVersion: ", lImplementation->LanguageVersion.Get().Buffer());
				HTR_LOG(Info, "            RenderName: ", lImplementation->RenderName.Buffer());
				HTR_LOG(Info, "            RenderAPI: ", lImplementation->RenderAPI.Get().Buffer());
				HTR_LOG(Info, "            RenderAPIVersion: ", lImplementation->RenderAPIVersion.Get().Buffer());

				const FbxBindingTable* lRootTable = lImplementation->GetRootTable();
				FbxString lFileName = lRootTable->DescAbsoluteURL.Get();
				FbxString lTechniqueName = lRootTable->DescTAG.Get();


				const FbxBindingTable* lTable = lImplementation->GetRootTable();
				size_t lEntryNum = lTable->GetEntryCount();

				for (int i = 0; i < (int)lEntryNum; ++i)
				{
					const FbxBindingTableEntry& lEntry = lTable->GetEntry(i);
					const char* lEntrySrcType = lEntry.GetEntryType(true);
					FbxProperty lFbxProp;


					FbxString lTest = lEntry.GetSource();
					HTR_LOG(Info, "            Entry: ", lTest.Buffer());


					if (strcmp(FbxPropertyEntryView::sEntryType, lEntrySrcType) == 0)
					{
						lFbxProp = lMaterial->FindPropertyHierarchical(lEntry.GetSource());
						if (!lFbxProp.IsValid())
						{
							lFbxProp = lMaterial->RootProperty.FindHierarchical(lEntry.GetSource());
						}


					}
					else if (strcmp(FbxConstantEntryView::sEntryType, lEntrySrcType) == 0)
					{
						lFbxProp = lImplementation->GetConstants().FindHierarchical(lEntry.GetSource());
					}
					if (lFbxProp.IsValid())
					{
						if (lFbxProp.GetSrcObjectCount<FbxTexture>() > 0)
						{
							//do what you want with the textures
							for (int j = 0; j < lFbxProp.GetSrcObjectCount<FbxFileTexture>(); ++j)
							{
								FbxFileTexture* lTex = lFbxProp.GetSrcObject<FbxFileTexture>(j);
								HTR_LOG(Info, "           File Texture: ", lTex->GetFileName());
							}
							for (int j = 0; j < lFbxProp.GetSrcObjectCount<FbxLayeredTexture>(); ++j)
							{
								FbxLayeredTexture* lTex = lFbxProp.GetSrcObject<FbxLayeredTexture>(j);
								HTR_LOG(Info, "        Layered Texture: ", lTex->GetName());
							}
							for (int j = 0; j < lFbxProp.GetSrcObjectCount<FbxProceduralTexture>(); ++j)
							{
								FbxProceduralTexture* lTex = lFbxProp.GetSrcObject<FbxProceduralTexture>(j);
								HTR_LOG(Info, "     Procedural Texture: ", lTex->GetName());
							}
						}
						else
						{
							FbxDataType lFbxType = lFbxProp.GetPropertyDataType();
							FbxString blah = lFbxType.GetName();
							if (FbxBoolDT == lFbxType)
							{
								DisplayBool("                Bool: ", lFbxProp.Get<FbxBool>());
							}
							else if (FbxIntDT == lFbxType || FbxEnumDT == lFbxType)
							{
								HTR_LOG(Info, "                Int: {}", lFbxProp.Get<FbxInt>());
							}
							else if (FbxFloatDT == lFbxType)
							{
								DisplayDouble("                Float: ", lFbxProp.Get<FbxFloat>());

							}
							else if (FbxDoubleDT == lFbxType)
							{
								DisplayDouble("                Double: ", lFbxProp.Get<FbxDouble>());
							}
							else if (FbxStringDT == lFbxType
									 || FbxUrlDT == lFbxType
									 || FbxXRefUrlDT == lFbxType)
							{
								HTR_LOG(Info, "                String: {}", lFbxProp.Get<FbxInt>());
							}
							else if (FbxDouble2DT == lFbxType)
							{
								FbxDouble2 lDouble2 = lFbxProp.Get<FbxDouble2>();
								FbxVector2 lVect;
								lVect[0] = lDouble2[0];
								lVect[1] = lDouble2[1];

								DisplayVector2("                2D vector: ", lVect);
							}
							else if (FbxDouble3DT == lFbxType || FbxColor3DT == lFbxType)
							{
								FbxDouble3 lDouble3 = lFbxProp.Get<FbxDouble3>();


								FbxVector4 lVect;
								lVect[0] = lDouble3[0];
								lVect[1] = lDouble3[1];
								lVect[2] = lDouble3[2];
								DisplayVector3("                3D vector: ", lVect);
							}

							else if (FbxDouble4DT == lFbxType || FbxColor4DT == lFbxType)
							{
								FbxDouble4 lDouble4 = lFbxProp.Get<FbxDouble4>();
								FbxVector4 lVect;
								lVect[0] = lDouble4[0];
								lVect[1] = lDouble4[1];
								lVect[2] = lDouble4[2];
								lVect[3] = lDouble4[3];
								DisplayVector4("                4D vector: ", lVect);
							}
							else if (FbxDouble4x4DT == lFbxType)
							{
								FbxDouble4x4 lDouble44 = lFbxProp.Get<FbxDouble4x4>();
								for (int j = 0; j < 4; ++j)
								{

									FbxVector4 lVect;
									lVect[0] = lDouble44[j][0];
									lVect[1] = lDouble44[j][1];
									lVect[2] = lDouble44[j][2];
									lVect[3] = lDouble44[j][3];
									DisplayVector4("                4x4D vector: ", lVect);
								}

							}
						}

					}
				}
			}
			else if (lMaterial->GetClassId().Is(FbxSurfacePhong::ClassId))
			{
				// We found a Phong material.  Display its properties.

				// Display the Ambient Color
				lKFbxDouble3 = ((FbxSurfacePhong*)lMaterial)->Ambient;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Ambient: ", theColor);

				// Display the Diffuse Color
				lKFbxDouble3 = ((FbxSurfacePhong*)lMaterial)->Diffuse;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Diffuse: ", theColor);

				// Display the Specular Color (unique to Phong materials)
				lKFbxDouble3 = ((FbxSurfacePhong*)lMaterial)->Specular;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Specular: ", theColor);

				// Display the Emissive Color
				lKFbxDouble3 = ((FbxSurfacePhong*)lMaterial)->Emissive;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Emissive: ", theColor);

				//Opacity is Transparency factor now
				lKFbxDouble1 = ((FbxSurfacePhong*)lMaterial)->TransparencyFactor;
				DisplayDouble("            Opacity: ", 1.0 - lKFbxDouble1.Get());

				// Display the Shininess
				lKFbxDouble1 = ((FbxSurfacePhong*)lMaterial)->Shininess;
				DisplayDouble("            Shininess: ", lKFbxDouble1.Get());

				// Display the Reflectivity
				lKFbxDouble1 = ((FbxSurfacePhong*)lMaterial)->ReflectionFactor;
				DisplayDouble("            Reflectivity: ", lKFbxDouble1.Get());
			}
			else if (lMaterial->GetClassId().Is(FbxSurfaceLambert::ClassId))
			{
				// We found a Lambert material. Display its properties.
				// Display the Ambient Color
				lKFbxDouble3 = ((FbxSurfaceLambert*)lMaterial)->Ambient;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Ambient: ", theColor);

				// Display the Diffuse Color
				lKFbxDouble3 = ((FbxSurfaceLambert*)lMaterial)->Diffuse;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Diffuse: ", theColor);

				// Display the Emissive
				lKFbxDouble3 = ((FbxSurfaceLambert*)lMaterial)->Emissive;
				theColor.Set(lKFbxDouble3.Get()[0], lKFbxDouble3.Get()[1], lKFbxDouble3.Get()[2]);
				DisplayColor("            Emissive: ", theColor);

				// Display the Opacity
				lKFbxDouble1 = ((FbxSurfaceLambert*)lMaterial)->TransparencyFactor;
				DisplayDouble("            Opacity: ", 1.0 - lKFbxDouble1.Get());
			}
			else
				HTR_LOG(Info, "Unknown type of Material");

			FbxPropertyT<FbxString> lString;
			lString = lMaterial->ShadingModel;
			HTR_LOG(Info, "            Shading Model: {}", lString.Get().Buffer());
		}
	}
}

const fbxsdk::FbxImplementation* FFbxDebugScene::LookForImplementation(fbxsdk::FbxSurfaceMaterial* pMaterial)
{
	const FbxImplementation* lImplementation = nullptr;
	if (!lImplementation) lImplementation = GetImplementation(pMaterial, FBXSDK_IMPLEMENTATION_CGFX);
	if (!lImplementation) lImplementation = GetImplementation(pMaterial, FBXSDK_IMPLEMENTATION_HLSL);
	if (!lImplementation) lImplementation = GetImplementation(pMaterial, FBXSDK_IMPLEMENTATION_SFX);
	if (!lImplementation) lImplementation = GetImplementation(pMaterial, FBXSDK_IMPLEMENTATION_OGS);
	if (!lImplementation) lImplementation = GetImplementation(pMaterial, FBXSDK_IMPLEMENTATION_SSSL);
	return lImplementation;
}

void FFbxDebugScene::DisplayTexture(fbxsdk::FbxGeometry* pGeometry)
{
	FbxNode* Node = pGeometry->GetNode();
	if (Node == nullptr) return;

	const int MaterialCount = Node->GetSrcObjectCount<FbxSurfaceMaterial>();
	for (int MaterialIndex = 0; MaterialIndex < MaterialCount; MaterialIndex++)
	{
		FbxSurfaceMaterial* Material = Node->GetSrcObject<FbxSurfaceMaterial>(MaterialIndex);
		if (Material == nullptr) continue;

		bool bDisplayHeader = true;

		// Diffuse, NormalMap 등 텍스처를 꽂을 수 있는 모든 채널을 돈다 (샘플의 FBXSDK_FOR_EACH_TEXTURE)
		for (int TextureIndex = 0; TextureIndex < FbxLayerElement::sTypeTextureCount; TextureIndex++)
		{
			FbxProperty Property = Material->FindProperty(FbxLayerElement::sTextureChannelNames[TextureIndex]);
			FindAndDisplayTextureInfoByProperty(Property, bDisplayHeader, MaterialIndex);
		}
	}
}

void FFbxDebugScene::FindAndDisplayTextureInfoByProperty(fbxsdk::FbxProperty& pProperty, bool& pDisplayHeader, int pMaterialIndex)
{
	if (!pProperty.IsValid()) return;

	const int TextureCount = pProperty.GetSrcObjectCount<FbxTexture>();
	for (int j = 0; j < TextureCount; j++)
	{
		// 여러 장을 섞는 LayeredTexture인지, 한 장짜리 텍스처인지 나눠서 본다
		FbxLayeredTexture* LayeredTexture = pProperty.GetSrcObject<FbxLayeredTexture>(j);
		if (LayeredTexture)
		{
			HTR_LOG(Info, "    Layered Texture: {}", j);

			const int LayerCount = LayeredTexture->GetSrcObjectCount<FbxTexture>();
			for (int k = 0; k < LayerCount; k++)
			{
				FbxTexture* Texture = LayeredTexture->GetSrcObject<FbxTexture>(k);
				if (Texture == nullptr) continue;

				if (pDisplayHeader)
				{
					HTR_LOG(Info, "    Textures connected to Material {}", pMaterialIndex);
					pDisplayHeader = false;
				}

				// 블렌드 모드는 텍스처가 아니라 LayeredTexture에 있다. 한 텍스처가 여러 LayeredTexture에 다른 모드로 들어갈 수 있기 때문이다.
				FbxLayeredTexture::EBlendMode BlendMode;
				LayeredTexture->GetTextureBlendMode(k, BlendMode);
				HTR_LOG(Info, "    Textures for {}", pProperty.GetName().Buffer());
				HTR_LOG(Info, "        Texture {}", k);
				DisplayTextureInfo(Texture, static_cast<int>(BlendMode));
			}
		}
		else
		{
			FbxTexture* Texture = pProperty.GetSrcObject<FbxTexture>(j);
			if (Texture == nullptr) continue;

			if (pDisplayHeader)
			{
				HTR_LOG(Info, "    Textures connected to Material {}", pMaterialIndex);
				pDisplayHeader = false;
			}

			HTR_LOG(Info, "    Textures for {}", pProperty.GetName().Buffer());
			HTR_LOG(Info, "        Texture {}", j);
			DisplayTextureInfo(Texture, -1);
		}
	}
}

void FFbxDebugScene::DisplayTextureInfo(fbxsdk::FbxTexture* pTexture, int pBlendMode)
{
	FbxFileTexture* FileTexture = FbxCast<FbxFileTexture>(pTexture);
	FbxProceduralTexture* ProceduralTexture = FbxCast<FbxProceduralTexture>(pTexture);

	HTR_LOG(Info, "            Name: \"{}\"", pTexture->GetName());
	if (FileTexture)
	{
		HTR_LOG(Info, "            Type: File Texture");
		HTR_LOG(Info, "            File Name: \"{}\"", FileTexture->GetFileName());
		HTR_LOG(Info, "            Relative File Name: \"{}\"", FileTexture->GetRelativeFileName());
	}
	else if (ProceduralTexture)
	{
		HTR_LOG(Info, "            Type: Procedural Texture");
	}

	// 어떤 UV 세트를 쓰는지. 비어 있으면 첫 UV 세트(기본값)를 쓴다.
	HTR_LOG(Info, "            UV Set: \"{}\"", pTexture->UVSet.Get().Buffer());

	DisplayDouble("            Scale U: ", pTexture->GetScaleU());
	DisplayDouble("            Scale V: ", pTexture->GetScaleV());
	DisplayDouble("            Translation U: ", pTexture->GetTranslationU());
	DisplayDouble("            Translation V: ", pTexture->GetTranslationV());
	DisplayBool("            Swap UV: ", pTexture->GetSwapUV());
	DisplayDouble("            Rotation U: ", pTexture->GetRotationU());
	DisplayDouble("            Rotation V: ", pTexture->GetRotationV());
	DisplayDouble("            Rotation W: ", pTexture->GetRotationW());

	const char* AlphaSources[] = { "None", "RGB Intensity", "Black" };
	HTR_LOG(Info, "            Alpha Source: {}", AlphaSources[pTexture->GetAlphaSource()]);

	DisplayDouble("            Cropping Left: ", pTexture->GetCroppingLeft());
	DisplayDouble("            Cropping Top: ", pTexture->GetCroppingTop());
	DisplayDouble("            Cropping Right: ", pTexture->GetCroppingRight());
	DisplayDouble("            Cropping Bottom: ", pTexture->GetCroppingBottom());

	const char* MappingTypes[] = { "Null", "Planar", "Spherical", "Cylindrical", "Box", "Face", "UV", "Environment" };
	HTR_LOG(Info, "            Mapping Type: {}", MappingTypes[pTexture->GetMappingType()]);

	if (pTexture->GetMappingType() == FbxTexture::ePlanar)
	{
		const char* PlanarMappingNormals[] = { "X", "Y", "Z" };
		HTR_LOG(Info, "            Planar Mapping Normal: {}", PlanarMappingNormals[pTexture->GetPlanarMappingNormal()]);
	}

	const char* BlendModes[] = { "Translucent", "Additive", "Modulate", "Modulate2", "Over", "Normal", "Dissolve", "Darken", "ColorBurn", "LinearBurn",
		"DarkerColor", "Lighten", "Screen", "ColorDodge", "LinearDodge", "LighterColor", "SoftLight", "HardLight", "VividLight",
		"LinearLight", "PinLight", "HardMix", "Difference", "Exclusion", "Substract", "Divide", "Hue", "Saturation", "Color",
		"Luminosity", "Overlay" };

	if (pBlendMode >= 0)
	{
		HTR_LOG(Info, "            Blend Mode: {}", BlendModes[pBlendMode]);
	}
	DisplayDouble("            Alpha: ", pTexture->GetDefaultAlpha());

	if (FileTexture)
	{
		const char* MaterialUses[] = { "Model Material", "Default Material" };
		HTR_LOG(Info, "            Material Use: {}", MaterialUses[FileTexture->GetMaterialUse()]);
	}

	const char* TextureUses[] = { "Standard", "Shadow Map", "Light Map", "Spherical Reflexion Map", "Sphere Reflexion Map", "Bump Normal Map" };
	HTR_LOG(Info, "            Texture Use: {}", TextureUses[pTexture->GetTextureUse()]);
}

void FFbxDebugScene::DisplayMaterialConnections(fbxsdk::FbxMesh* pMesh)
{
	const int PolygonCount = pMesh->GetPolygonCount();
	const int MaterialElementCount = pMesh->GetElementMaterialCount();

	HTR_LOG(Info, "    Polygons Material Connections");

	if (MaterialElementCount == 0)
	{
		HTR_LOG(Info, "        no material applied");
		return;
	}

	// 머티리얼 요소가 하나라도 ByPolygon이면 폴리곤마다 찍고, 전부 AllSame이면 한 번만 찍는다
	bool bIsAllSame = true;
	for (int l = 0; l < MaterialElementCount; l++)
	{
		if (pMesh->GetElementMaterial(l)->GetMappingMode() == FbxGeometryElement::eByPolygon)
		{
			bIsAllSame = false;
			break;
		}
	}

	if (bIsAllSame)
	{
		for (int l = 0; l < MaterialElementCount; l++)
		{
			FbxGeometryElementMaterial* MaterialElement = pMesh->GetElementMaterial(l);
			if (MaterialElement->GetMappingMode() != FbxGeometryElement::eAllSame) continue;

			const int MatId = MaterialElement->GetIndexArray().GetAt(0);
			if (MatId < 0) continue;

			HTR_LOG(Info, "        All polygons share the same material in mesh {}", l);
			DisplayMaterialTextureConnections(pMesh->GetNode()->GetMaterial(MatId), MatId, l);
		}
		return;
	}

	for (int i = 0; i < PolygonCount; i++)
	{
		HTR_LOG(Info, "        Polygon {}", i);

		for (int l = 0; l < MaterialElementCount; l++)
		{
			FbxGeometryElementMaterial* MaterialElement = pMesh->GetElementMaterial(l);

			// ByPolygon과 AllSame 요소가 섞여 있을 수 있다. AllSame은 IndexArray가 1개뿐이라 0번을 읽는다.
			const int IndexInArray = MaterialElement->GetMappingMode() == FbxGeometryElement::eAllSame ? 0 : i;
			const int MatId = MaterialElement->GetIndexArray().GetAt(IndexInArray);
			if (MatId < 0) continue;

			DisplayMaterialTextureConnections(pMesh->GetNode()->GetMaterial(MatId), MatId, l);
		}
	}
}

void FFbxDebugScene::DisplayMaterialTextureConnections(fbxsdk::FbxSurfaceMaterial* pMaterial, int pMatId, int pElementIndex)
{
	if (pMaterial == nullptr) return;

	// 텍스처를 꽂을 수 있는 머티리얼 속성들. 샘플은 하나씩 나열했지만 같은 처리라 배열로 돈다.
	const char* TextureProperties[] =
	{
		FbxSurfaceMaterial::sDiffuse, FbxSurfaceMaterial::sDiffuseFactor,
		FbxSurfaceMaterial::sEmissive, FbxSurfaceMaterial::sEmissiveFactor,
		FbxSurfaceMaterial::sAmbient, FbxSurfaceMaterial::sAmbientFactor,
		FbxSurfaceMaterial::sSpecular, FbxSurfaceMaterial::sSpecularFactor,
		FbxSurfaceMaterial::sShininess,
		FbxSurfaceMaterial::sBump, FbxSurfaceMaterial::sNormalMap,
		FbxSurfaceMaterial::sTransparentColor, FbxSurfaceMaterial::sTransparencyFactor,
		FbxSurfaceMaterial::sReflection, FbxSurfaceMaterial::sReflectionFactor,
	};

	FbxString ConnectionString;
	for (const char* PropertyName : TextureProperties)
	{
		FbxProperty Property = pMaterial->FindProperty(PropertyName);
		DisplayTextureNames(Property, ConnectionString);
	}

	HTR_LOG(Info, "            Material {} (element {}) --{}", pMatId, pElementIndex, ConnectionString.Buffer());
}

void FFbxDebugScene::DisplayTextureNames(fbxsdk::FbxProperty& pProperty, fbxsdk::FbxString& pConnectionString)
{
	const int LayeredTextureCount = pProperty.GetSrcObjectCount<FbxLayeredTexture>();
	if (LayeredTextureCount > 0)
	{
		for (int j = 0; j < LayeredTextureCount; j++)
		{
			FbxLayeredTexture* LayeredTexture = pProperty.GetSrcObject<FbxLayeredTexture>(j);
			const int TextureCount = LayeredTexture->GetSrcObjectCount<FbxTexture>();

			pConnectionString += " Texture ";
			for (int k = 0; k < TextureCount; k++)
			{
				pConnectionString += "\"";
				pConnectionString += LayeredTexture->GetName();
				pConnectionString += "\" ";
			}
			pConnectionString += "of ";
			pConnectionString += pProperty.GetName();
			pConnectionString += " on layer ";
			pConnectionString += j;
		}
		pConnectionString += " |";
		return;
	}

	const int TextureCount = pProperty.GetSrcObjectCount<FbxTexture>();
	if (TextureCount == 0) return;

	pConnectionString += " Texture  ";
	for (int j = 0; j < TextureCount; j++)
	{
		FbxTexture* Texture = pProperty.GetSrcObject<FbxTexture>(j);
		if (Texture == nullptr) continue;

		pConnectionString += "\"";
		pConnectionString += Texture->GetName();
		pConnectionString += "\" ";
	}
	pConnectionString += "of ";
	pConnectionString += pProperty.GetName();
	pConnectionString += " |";
}

void FFbxDebugScene::DisplayTexturePaths(fbxsdk::FbxScene* InScene, const FString& FbxPath)
{
	// SDK 경로 문자열은 UTF-8이라 char8_t로 바꿔 fs::path를 만든다
	auto ToPath = [](const char* Utf8) { return fs::path(reinterpret_cast<const char8_t*>(Utf8)); };

	const fs::path FbxDirectory = fs::path(FbxPath).parent_path();
	const int TextureCount = InScene->GetSrcObjectCount<FbxFileTexture>();

	HTR_LOG(Info, "File Textures: {}", TextureCount);

	for (int i = 0; i < TextureCount; i++)
	{
		FbxFileTexture* Texture = InScene->GetSrcObject<FbxFileTexture>(i);
		if (Texture == nullptr) continue;

		const char* FileName = Texture->GetFileName();
		const char* RelativeFileName = Texture->GetRelativeFileName();

		HTR_LOG(Info, "    Texture {} \"{}\"", i, Texture->GetName());
		HTR_LOG(Info, "        File Name: \"{}\"", FileName);
		HTR_LOG(Info, "        Relative File Name: \"{}\"", RelativeFileName);

		// 임베디드 텍스처는 Import 때 .fbm으로 추출되면서 File Name이 그 경로로 바뀐다
		std::error_code Error;
		fs::path FoundPath;
		if (FileName && *FileName && fs::exists(ToPath(FileName), Error))
		{
			FoundPath = ToPath(FileName);
			HTR_LOG(Info, "        File Name exists: true");
		}
		else
		{
			HTR_LOG(Info, "        File Name exists: false");
		}

		if (RelativeFileName && *RelativeFileName)
		{
			const fs::path RelativeResolved = FbxDirectory / ToPath(RelativeFileName);
			const bool bRelativeExists = fs::exists(RelativeResolved, Error);
			HTR_LOG(Info, "        Relative (from fbx folder): \"{}\" exists: {}", RelativeResolved.generic_string(), bRelativeExists ? "true" : "false");

			if (FoundPath.empty() && bRelativeExists)
			{
				FoundPath = RelativeResolved;
			}
		}

		// 엔진은 저장소 루트(실행 시 작업 폴더) 기준 경로를 쓴다
		if (FoundPath.empty())
		{
			HTR_LOG(Warning, "        Texture file not found: \"{}\"", Texture->GetName());
		}
		else
		{
			HTR_LOG(Info, "        Engine Path: \"{}\"", fs::relative(FoundPath, fs::current_path(), Error).generic_string());
		}
	}
}
