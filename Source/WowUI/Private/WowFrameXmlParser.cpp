#include "WowFrameXmlParser.h"
#include "Mpq/MpqManager.h"
#include "XmlParser.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

#if HAS_PUGIXML
THIRD_PARTY_INCLUDES_START
#include "pugixml.hpp"
THIRD_PARTY_INCLUDES_END
#endif

DEFINE_LOG_CATEGORY_STATIC(LogWowXml, Log, All);

static bool IsFrameTypeTag(const FString& Tag);
static FWowFrameDef ParseFrameNode(const FXmlNode* Node);

namespace
{
FString NormalizeWowPath(const FString& InPath)
{
	FString Normalized = InPath;
	Normalized.ReplaceInline(TEXT("/"), TEXT("\\"));
	Normalized.ReplaceInline(TEXT("\\.\\"), TEXT("\\"));

	while (Normalized.Contains(TEXT("\\\\")))
	{
		Normalized.ReplaceInline(TEXT("\\\\"), TEXT("\\"));
	}

	TArray<FString> Segments;
	Normalized.ParseIntoArray(Segments, TEXT("\\"), true);

	TArray<FString> CleanSegments;
	for (const FString& Segment : Segments)
	{
		if (Segment.IsEmpty() || Segment == TEXT("."))
		{
			continue;
		}

		if (Segment == TEXT(".."))
		{
			if (CleanSegments.Num() > 0)
			{
				CleanSegments.Pop();
			}
			continue;
		}

		CleanSegments.Add(Segment);
	}

	return FString::Join(CleanSegments, TEXT("\\"));
}

FString GetWowDirectory(const FString& InPath)
{
	const FString Normalized = NormalizeWowPath(InPath);
	int32 SlashIndex = INDEX_NONE;
	if (Normalized.FindLastChar(TEXT('\\'), SlashIndex))
	{
		return Normalized.Left(SlashIndex + 1);
	}

	return FString();
}

FString ResolveWowPath(const FString& BaseDirectory, const FString& RelativePath)
{
	const FString Candidate = NormalizeWowPath(RelativePath);
	if (Candidate.StartsWith(TEXT("Interface\\"), ESearchCase::IgnoreCase))
	{
		return Candidate;
	}

	return NormalizeWowPath(BaseDirectory + Candidate);
}

bool TryReadWowFile(FMpqManager* Mpq, const FString& InPath, TArray<uint8>& OutData)
{
	if (!Mpq)
	{
		return false;
	}

	TArray<FString> Candidates;
	const FString Normalized = NormalizeWowPath(InPath);
	Candidates.AddUnique(Normalized);
	Candidates.AddUnique(Normalized.ToLower());
	Candidates.AddUnique(Normalized.Replace(TEXT("\\"), TEXT("/")));
	Candidates.AddUnique(Normalized.ToLower().Replace(TEXT("\\"), TEXT("/")));

	for (const FString& Candidate : Candidates)
	{
		if (Mpq->ReadFile(Candidate, OutData))
		{
			return true;
		}
	}

	for (const FString& Candidate : Candidates)
	{
		const FString FileSystemPath = FPaths::Combine(Mpq->GetDataPath(), Candidate.Replace(TEXT("\\"), TEXT("/")));
		if (FFileHelper::LoadFileToArray(OutData, *FileSystemPath))
		{
			return true;
		}
	}

	return false;
}

void AppendResolvedXmlDirectives(
	FMpqManager* Mpq,
	const FString& XmlPath,
	TSet<FString>& VisitedXmlFiles,
	TArray<FWowXmlDirective>& OutDirectives)
{
	if (!Mpq)
	{
		return;
	}

	const FString NormalizedXmlPath = NormalizeWowPath(XmlPath);
	const FString VisitKey = NormalizedXmlPath.ToLower();
	if (VisitedXmlFiles.Contains(VisitKey))
	{
		UE_LOG(LogWowXml, Verbose, TEXT("Skipping already processed XML include: %s"), *NormalizedXmlPath);
		return;
	}

	VisitedXmlFiles.Add(VisitKey);

	TArray<uint8> FileData;
	if (!TryReadWowFile(Mpq, NormalizedXmlPath, FileData))
	{
		UE_LOG(LogWowXml, Warning, TEXT("Could not read FrameXML file: %s"), *NormalizedXmlPath);
		return;
	}

	const FString BaseDirectory = GetWowDirectory(NormalizedXmlPath);
	const TArray<FWowXmlDirective> ParsedDirectives = FWowFrameXmlParser::ParseXml(FileData, NormalizedXmlPath);
	for (const FWowXmlDirective& Directive : ParsedDirectives)
	{
		if (Directive.Type == FWowXmlDirective::EType::Include)
		{
			if (!Directive.FilePath.IsEmpty())
			{
				AppendResolvedXmlDirectives(Mpq, ResolveWowPath(BaseDirectory, Directive.FilePath), VisitedXmlFiles, OutDirectives);
			}
			continue;
		}

		FWowXmlDirective ResolvedDirective = Directive;
		if (ResolvedDirective.Type == FWowXmlDirective::EType::Script && !ResolvedDirective.FilePath.IsEmpty())
		{
			ResolvedDirective.FilePath = ResolveWowPath(BaseDirectory, ResolvedDirective.FilePath);
		}

		OutDirectives.Add(MoveTemp(ResolvedDirective));
	}
}

TArray<FWowXmlDirective> ParseXmlWithFXmlFallback(const FString& XmlContent, const FString& FileName)
{
	TArray<FWowXmlDirective> Directives;

	FXmlFile XmlFile;
	if (!XmlFile.LoadFile(XmlContent, EConstructMethod::ConstructFromBuffer))
	{
		UE_LOG(LogWowXml, Warning, TEXT("Fallback XML parse also failed: %s - %s"), *FileName, *XmlFile.GetLastError());
		return Directives;
	}

	const FXmlNode* Root = XmlFile.GetRootNode();
	if (!Root)
	{
		UE_LOG(LogWowXml, Warning, TEXT("No root node in XML: %s"), *FileName);
		return Directives;
	}

	for (const FXmlNode* Child = Root->GetFirstChildNode(); Child; Child = Child->GetNextNode())
	{
		const FString Tag = Child->GetTag();

		if (IsFrameTypeTag(Tag))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Frame;
			Dir.FrameDef = ParseFrameNode(Child);
			Directives.Add(MoveTemp(Dir));
		}
		else if (Tag.Equals(TEXT("Include"), ESearchCase::IgnoreCase))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Include;
			Dir.FilePath = Child->GetAttribute(TEXT("file"));
			Directives.Add(MoveTemp(Dir));
		}
		else if (Tag.Equals(TEXT("Script"), ESearchCase::IgnoreCase))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Script;
			Dir.FilePath = Child->GetAttribute(TEXT("file"));
			Directives.Add(MoveTemp(Dir));
		}
		else if (Tag.Equals(TEXT("Font"), ESearchCase::IgnoreCase))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Font;
			Dir.FontName = Child->GetAttribute(TEXT("name"));
			Dir.FontInherits = Child->GetAttribute(TEXT("inherits"));

			const FString FontHeightAttr = Child->GetAttribute(TEXT("height"));
			if (!FontHeightAttr.IsEmpty())
			{
				Dir.FontHeight = FCString::Atof(*FontHeightAttr);
			}

			Dir.FontFlags = Child->GetAttribute(TEXT("flags"));

			const FXmlNode* FontHeightNode = Child->FindChildNode(TEXT("FontHeight"));
			if (FontHeightNode)
			{
				const FXmlNode* AbsValue = FontHeightNode->FindChildNode(TEXT("AbsValue"));
				if (AbsValue)
				{
					const FString Value = AbsValue->GetAttribute(TEXT("val"));
					if (!Value.IsEmpty())
					{
						Dir.FontHeight = FCString::Atof(*Value);
					}
				}
			}

			Directives.Add(MoveTemp(Dir));
		}
	}

	UE_LOG(LogWowXml, Log, TEXT("Fallback parsed %s: %d directives"), *FileName, Directives.Num());
	return Directives;
}
} // namespace

// ─── Enum parsers ──────────────────────────────────────────────────────────────

EWowFrameType FWowFrameXmlParser::ParseFrameType(const FString& TypeName)
{
	static const TMap<FString, EWowFrameType> Map = {
		{TEXT("Frame"),                  EWowFrameType::Frame},
		{TEXT("Button"),                 EWowFrameType::Button},
		{TEXT("CheckButton"),            EWowFrameType::CheckButton},
		{TEXT("EditBox"),                EWowFrameType::EditBox},
		{TEXT("ScrollFrame"),            EWowFrameType::ScrollFrame},
		{TEXT("ScrollingMessageFrame"),  EWowFrameType::ScrollingMessageFrame},
		{TEXT("MessageFrame"),           EWowFrameType::MessageFrame},
		{TEXT("SimpleHTML"),             EWowFrameType::SimpleHTML},
		{TEXT("Slider"),                 EWowFrameType::Slider},
		{TEXT("StatusBar"),              EWowFrameType::StatusBar},
		{TEXT("Cooldown"),               EWowFrameType::Cooldown},
		{TEXT("ColorSelect"),            EWowFrameType::ColorSelect},
		{TEXT("GameTooltip"),            EWowFrameType::GameTooltip},
		{TEXT("Minimap"),                EWowFrameType::Minimap},
		{TEXT("Model"),                  EWowFrameType::Model},
		{TEXT("PlayerModel"),            EWowFrameType::PlayerModel},
		{TEXT("DressUpModel"),           EWowFrameType::DressUpModel},
		{TEXT("TabardModel"),            EWowFrameType::TabardModel},
		{TEXT("WorldFrame"),             EWowFrameType::WorldFrame},
	};
	if (const EWowFrameType* Found = Map.Find(TypeName))
	{
		return *Found;
	}
	return EWowFrameType::Frame;
}

EWowAnchorPoint FWowFrameXmlParser::ParseAnchorPoint(const FString& PointName)
{
	static const TMap<FString, EWowAnchorPoint> Map = {
		{TEXT("TOPLEFT"),     EWowAnchorPoint::TOPLEFT},
		{TEXT("TOP"),         EWowAnchorPoint::TOP},
		{TEXT("TOPRIGHT"),    EWowAnchorPoint::TOPRIGHT},
		{TEXT("LEFT"),        EWowAnchorPoint::LEFT},
		{TEXT("CENTER"),      EWowAnchorPoint::CENTER},
		{TEXT("RIGHT"),       EWowAnchorPoint::RIGHT},
		{TEXT("BOTTOMLEFT"),  EWowAnchorPoint::BOTTOMLEFT},
		{TEXT("BOTTOM"),      EWowAnchorPoint::BOTTOM},
		{TEXT("BOTTOMRIGHT"), EWowAnchorPoint::BOTTOMRIGHT},
	};
	if (const EWowAnchorPoint* Found = Map.Find(PointName.ToUpper()))
	{
		return *Found;
	}
	return EWowAnchorPoint::CENTER;
}

EWowFrameStrata FWowFrameXmlParser::ParseStrata(const FString& StrataName)
{
	static const TMap<FString, EWowFrameStrata> Map = {
		{TEXT("BACKGROUND"),         EWowFrameStrata::BACKGROUND},
		{TEXT("LOW"),                EWowFrameStrata::LOW},
		{TEXT("MEDIUM"),             EWowFrameStrata::MEDIUM},
		{TEXT("HIGH"),               EWowFrameStrata::HIGH},
		{TEXT("DIALOG"),             EWowFrameStrata::DIALOG},
		{TEXT("FULLSCREEN"),         EWowFrameStrata::FULLSCREEN},
		{TEXT("FULLSCREEN_DIALOG"),  EWowFrameStrata::FULLSCREEN_DIALOG},
		{TEXT("TOOLTIP"),            EWowFrameStrata::TOOLTIP},
	};
	if (const EWowFrameStrata* Found = Map.Find(StrataName.ToUpper()))
	{
		return *Found;
	}
	return EWowFrameStrata::MEDIUM;
}

EWowDrawLayer FWowFrameXmlParser::ParseDrawLayer(const FString& LayerName)
{
	static const TMap<FString, EWowDrawLayer> Map = {
		{TEXT("BACKGROUND"), EWowDrawLayer::BACKGROUND},
		{TEXT("BORDER"),     EWowDrawLayer::BORDER},
		{TEXT("ARTWORK"),    EWowDrawLayer::ARTWORK},
		{TEXT("OVERLAY"),    EWowDrawLayer::OVERLAY},
		{TEXT("HIGHLIGHT"),  EWowDrawLayer::HIGHLIGHT},
	};
	if (const EWowDrawLayer* Found = Map.Find(LayerName.ToUpper()))
	{
		return *Found;
	}
	return EWowDrawLayer::ARTWORK;
}

// ─── Helper: parse color from node attributes ──────────────────────────────────

static FLinearColor ParseColor(const FXmlNode* Node)
{
	FLinearColor C = FLinearColor::White;
	if (!Node) return C;
	FString R = Node->GetAttribute(TEXT("r"));
	FString G = Node->GetAttribute(TEXT("g"));
	FString B = Node->GetAttribute(TEXT("b"));
	FString A = Node->GetAttribute(TEXT("a"));
	if (!R.IsEmpty()) C.R = FCString::Atof(*R);
	if (!G.IsEmpty()) C.G = FCString::Atof(*G);
	if (!B.IsEmpty()) C.B = FCString::Atof(*B);
	if (!A.IsEmpty()) C.A = FCString::Atof(*A);
	return C;
}

// ─── Helper: parse Size node ───────────────────────────────────────────────────

static void ParseSize(const FXmlNode* SizeNode, float& OutW, float& OutH)
{
	if (!SizeNode) return;
	FString X = SizeNode->GetAttribute(TEXT("x"));
	FString Y = SizeNode->GetAttribute(TEXT("y"));
	if (!X.IsEmpty()) OutW = FCString::Atof(*X);
	if (!Y.IsEmpty()) OutH = FCString::Atof(*Y);

	// Also check for <AbsDimension> child
	const FXmlNode* AbsDim = SizeNode->FindChildNode(TEXT("AbsDimension"));
	if (AbsDim)
	{
		X = AbsDim->GetAttribute(TEXT("x"));
		Y = AbsDim->GetAttribute(TEXT("y"));
		if (!X.IsEmpty()) OutW = FCString::Atof(*X);
		if (!Y.IsEmpty()) OutH = FCString::Atof(*Y);
	}
}

// ─── Helper: parse Anchor node ─────────────────────────────────────────────────

static FWowAnchor ParseAnchorNode(const FXmlNode* Node)
{
	FWowAnchor Anchor;
	Anchor.Point = FWowFrameXmlParser::ParseAnchorPoint(Node->GetAttribute(TEXT("point")));
	Anchor.RelativeTo = Node->GetAttribute(TEXT("relativeTo"));

	FString RelPoint = Node->GetAttribute(TEXT("relativePoint"));
	if (!RelPoint.IsEmpty())
	{
		Anchor.RelativePoint = FWowFrameXmlParser::ParseAnchorPoint(RelPoint);
	}
	else
	{
		Anchor.RelativePoint = Anchor.Point;
	}

	// Offset from attributes
	FString OX = Node->GetAttribute(TEXT("x"));
	FString OY = Node->GetAttribute(TEXT("y"));
	if (!OX.IsEmpty()) Anchor.OffsetX = FCString::Atof(*OX);
	if (!OY.IsEmpty()) Anchor.OffsetY = FCString::Atof(*OY);

	// Or from <Offset> child
	const FXmlNode* OffsetNode = Node->FindChildNode(TEXT("Offset"));
	if (OffsetNode)
	{
		const FXmlNode* AbsDim = OffsetNode->FindChildNode(TEXT("AbsDimension"));
		if (AbsDim)
		{
			OX = AbsDim->GetAttribute(TEXT("x"));
			OY = AbsDim->GetAttribute(TEXT("y"));
		}
		else
		{
			OX = OffsetNode->GetAttribute(TEXT("x"));
			OY = OffsetNode->GetAttribute(TEXT("y"));
		}
		if (!OX.IsEmpty()) Anchor.OffsetX = FCString::Atof(*OX);
		if (!OY.IsEmpty()) Anchor.OffsetY = FCString::Atof(*OY);
	}

	return Anchor;
}

// ─── Helper: parse anchors list ────────────────────────────────────────────────

static TArray<FWowAnchor> ParseAnchors(const FXmlNode* AnchorsNode)
{
	TArray<FWowAnchor> Result;
	if (!AnchorsNode) return Result;

	for (const FXmlNode* Child = AnchorsNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
	{
		if (Child->GetTag().Equals(TEXT("Anchor"), ESearchCase::IgnoreCase))
		{
			Result.Add(ParseAnchorNode(Child));
		}
	}
	return Result;
}

// ─── Helper: parse Texture element ─────────────────────────────────────────────

static FWowTextureElement ParseTextureNode(const FXmlNode* Node)
{
	FWowTextureElement Tex;
	Tex.Name = Node->GetAttribute(TEXT("name"));
	Tex.ParentKey = Node->GetAttribute(TEXT("parentKey"));
	Tex.File = Node->GetAttribute(TEXT("file"));

	// Size from attributes or child
	const FXmlNode* SizeNode = Node->FindChildNode(TEXT("Size"));
	ParseSize(SizeNode, Tex.Width, Tex.Height);

	// Anchors
	const FXmlNode* AnchorsNode = Node->FindChildNode(TEXT("Anchors"));
	Tex.Anchors = ParseAnchors(AnchorsNode);

	// TexCoords
	const FXmlNode* TexCoords = Node->FindChildNode(TEXT("TexCoords"));
	if (TexCoords)
	{
		FString L = TexCoords->GetAttribute(TEXT("left"));
		FString R = TexCoords->GetAttribute(TEXT("right"));
		FString T = TexCoords->GetAttribute(TEXT("top"));
		FString B = TexCoords->GetAttribute(TEXT("bottom"));
		if (!L.IsEmpty()) Tex.Left = FCString::Atof(*L);
		if (!R.IsEmpty()) Tex.Right = FCString::Atof(*R);
		if (!T.IsEmpty()) Tex.Top = FCString::Atof(*T);
		if (!B.IsEmpty()) Tex.Bottom = FCString::Atof(*B);
	}

	// Color
	const FXmlNode* ColorNode = Node->FindChildNode(TEXT("Color"));
	if (ColorNode)
	{
		Tex.VertexColor = ParseColor(ColorNode);
	}

	return Tex;
}

// Virtual <Texture> templates usable by button state textures, by name.
static TMap<FString, TPair<FString, FVector4>>& WowVirtualButtonTextures()
{
	static TMap<FString, TPair<FString, FVector4>> Textures;
	return Textures;
}

#if HAS_PUGIXML
// r379: a button state texture's <Size> and first <Anchor> (point, relative point, offset).
static void ReadButtonTextureLayout(const pugi::xml_node& Node, FWowFrameDef& Def)
{
	if (const pugi::xml_node Size = Node.child("Size"))
	{
		const pugi::xml_node Abs = Size.child("AbsDimension");
		Def.NormalTextureW = (Abs ? Abs : Size).attribute("x").as_float(0.0f);
		Def.NormalTextureH = (Abs ? Abs : Size).attribute("y").as_float(0.0f);
	}
	if (const pugi::xml_node Anchor = Node.child("Anchors").child("Anchor"))
	{
		Def.NormalTexturePoint = FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(Anchor.attribute("point").as_string("CENTER")));
		const char* Rel = Anchor.attribute("relativePoint").as_string("");
		Def.NormalTextureRelativePoint = Rel[0] ? FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(Rel)) : Def.NormalTexturePoint;
		const pugi::xml_node Offset = Anchor.child("Offset");
		const pugi::xml_node OffsetAbs = Offset ? Offset.child("AbsDimension") : Anchor.child("AbsDimension");
		const pugi::xml_node OffsetNode = OffsetAbs ? OffsetAbs : Offset;
		if (OffsetNode) Def.NormalTextureOffset = FVector2D(OffsetNode.attribute("x").as_float(0.0f), OffsetNode.attribute("y").as_float(0.0f));
	}
}
static void ResolveVirtualButtonTexture(const pugi::xml_node& Node, FString& File, FVector4& UV)
{
	if (!File.IsEmpty()) return;
	const FString Inherits = UTF8_TO_TCHAR(Node.attribute("inherits").as_string());
	if (Inherits.IsEmpty()) return;
	if (const TPair<FString, FVector4>* Found = WowVirtualButtonTextures().Find(Inherits))
	{
		File = Found->Key;
		if (!Node.child("TexCoords")) UV = Found->Value;
	}
}
#endif

// ─── Helper: parse FontString element ──────────────────────────────────────────

static FWowFontStringElement ParseFontStringNode(const FXmlNode* Node)
{
	FWowFontStringElement FS;
	FS.Name = Node->GetAttribute(TEXT("name"));
	FS.ParentKey = Node->GetAttribute(TEXT("parentKey"));
	FS.Inherits = Node->GetAttribute(TEXT("inherits"));
	FS.Text = Node->GetAttribute(TEXT("text"));

	FString JH = Node->GetAttribute(TEXT("justifyH"));
	FString JV = Node->GetAttribute(TEXT("justifyV"));
	FS.JustifyH = JH; // empty: the font object decides (see FWowFrameManager, CreateLayerContent)
	if (!JV.IsEmpty()) FS.JustifyV = JV;

	// Size
	const FXmlNode* SizeNode = Node->FindChildNode(TEXT("Size"));
	ParseSize(SizeNode, FS.Width, FS.Height);

	// Anchors
	const FXmlNode* AnchorsNode = Node->FindChildNode(TEXT("Anchors"));
	FS.Anchors = ParseAnchors(AnchorsNode);

	// Color
	const FXmlNode* ColorNode = Node->FindChildNode(TEXT("Color"));
	if (ColorNode)
	{
		FS.Color = ParseColor(ColorNode);
		FS.bHasColor = true;
	}

	// FontHeight
	const FXmlNode* FontHeightNode = Node->FindChildNode(TEXT("FontHeight"));
	if (FontHeightNode)
	{
		const FXmlNode* AbsVal = FontHeightNode->FindChildNode(TEXT("AbsValue"));
		if (AbsVal)
		{
			FString Val = AbsVal->GetAttribute(TEXT("val"));
			if (!Val.IsEmpty()) { FS.FontHeight = FCString::Atof(*Val); FS.bHasFontHeight = true; }
		}
	}

	FString FontFlags = Node->GetAttribute(TEXT("outline"));
	if (!FontFlags.IsEmpty()) FS.FontFlags = FontFlags;

	return FS;
}

// ─── Helper: parse Layers ──────────────────────────────────────────────────────

static TArray<FWowLayer> ParseLayers(const FXmlNode* LayersNode)
{
	TArray<FWowLayer> Result;
	if (!LayersNode) return Result;

	for (const FXmlNode* LayerNode = LayersNode->GetFirstChildNode(); LayerNode; LayerNode = LayerNode->GetNextNode())
	{
		if (!LayerNode->GetTag().Equals(TEXT("Layer"), ESearchCase::IgnoreCase))
		{
			continue;
		}

		FWowLayer Layer;
		FString LevelStr = LayerNode->GetAttribute(TEXT("level"));
		if (!LevelStr.IsEmpty())
		{
			Layer.Level = FWowFrameXmlParser::ParseDrawLayer(LevelStr);
		}

		for (const FXmlNode* Elem = LayerNode->GetFirstChildNode(); Elem; Elem = Elem->GetNextNode())
		{
			FString Tag = Elem->GetTag();
			if (Tag.Equals(TEXT("Texture"), ESearchCase::IgnoreCase))
			{
				Layer.Textures.Add(ParseTextureNode(Elem));
			}
			else if (Tag.Equals(TEXT("FontString"), ESearchCase::IgnoreCase))
			{
				Layer.FontStrings.Add(ParseFontStringNode(Elem));
			}
		}

		Result.Add(MoveTemp(Layer));
	}
	return Result;
}

static void AppendInlineTexturesToLayers(FWowFrameDef& Def, TArray<FWowTextureElement>& InlineTextures)
{
	if (InlineTextures.Num() == 0)
	{
		return;
	}

	for (FWowLayer& Layer : Def.Layers)
	{
		if (Layer.Level == EWowDrawLayer::ARTWORK)
		{
			Layer.Textures.Append(InlineTextures);
			InlineTextures.Reset();
			return;
		}
	}

	FWowLayer InlineLayer;
	InlineLayer.Level = EWowDrawLayer::ARTWORK;
	InlineLayer.Textures = MoveTemp(InlineTextures);
	Def.Layers.Add(MoveTemp(InlineLayer));
}

// ─── Helper: parse Scripts ─────────────────────────────────────────────────────

static TArray<FWowScriptHandler> ParseScripts(const FXmlNode* ScriptsNode)
{
	TArray<FWowScriptHandler> Result;
	if (!ScriptsNode) return Result;

	for (const FXmlNode* Child = ScriptsNode->GetFirstChildNode(); Child; Child = Child->GetNextNode())
	{
		FWowScriptHandler Handler;
		Handler.Event = Child->GetTag();
		Handler.Code = Child->GetContent();
		Handler.File = Child->GetAttribute(TEXT("function"));
		// Some scripts use a "file" attribute instead
		FString FileAttr = Child->GetAttribute(TEXT("file"));
		if (!FileAttr.IsEmpty())
		{
			Handler.File = FileAttr;
		}
		Result.Add(MoveTemp(Handler));
	}
	return Result;
}

static void CollectNamedObjectGlobals(const FXmlNode* Node, TArray<FString>& OutNames)
{
	if (!Node)
	{
		return;
	}

	for (const FXmlNode* Child = Node->GetFirstChildNode(); Child; Child = Child->GetNextNode())
	{
		const FString Name = Child->GetAttribute(TEXT("name"));
		if (!Name.IsEmpty())
		{
			OutNames.AddUnique(Name);
		}

		CollectNamedObjectGlobals(Child, OutNames);
	}
}

static void CollectNamedObjectGlobals_Pugi(const pugi::xml_node& Node, TArray<FString>& OutNames)
{
	for (pugi::xml_node Child = Node.first_child(); Child; Child = Child.next_sibling())
	{
		if (Child.type() != pugi::node_element)
		{
			continue;
		}

		const FString Name = UTF8_TO_TCHAR(Child.attribute("name").as_string());
		if (!Name.IsEmpty())
		{
			OutNames.AddUnique(Name);
		}

		CollectNamedObjectGlobals_Pugi(Child, OutNames);
	}
}

// ─── Helper: parse Backdrop ────────────────────────────────────────────────────

static FWowBackdrop ParseBackdropNode(const FXmlNode* Node)
{
	FWowBackdrop BD;
	BD.BgFile = Node->GetAttribute(TEXT("bgFile"));
	BD.EdgeFile = Node->GetAttribute(TEXT("edgeFile"));

	FString ES = Node->GetAttribute(TEXT("edgeSize"));
	if (!ES.IsEmpty()) BD.EdgeSize = FCString::Atoi(*ES);

	FString Tile = Node->GetAttribute(TEXT("tile"));
	BD.Tile = Tile.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	FString TS = Node->GetAttribute(TEXT("tileSize"));
	if (!TS.IsEmpty()) BD.TileSize = FCString::Atoi(*TS);

	// BackgroundInsets
	const FXmlNode* Insets = Node->FindChildNode(TEXT("BackgroundInsets"));
	if (!Insets) Insets = Node->FindChildNode(TEXT("EdgeSize"));
	if (Insets)
	{
		const FXmlNode* AbsInset = Insets->FindChildNode(TEXT("AbsInset"));
		if (AbsInset)
		{
			FString L = AbsInset->GetAttribute(TEXT("left"));
			FString R = AbsInset->GetAttribute(TEXT("right"));
			FString T = AbsInset->GetAttribute(TEXT("top"));
			FString B = AbsInset->GetAttribute(TEXT("bottom"));
			if (!L.IsEmpty()) BD.InsetLeft = FCString::Atof(*L);
			if (!R.IsEmpty()) BD.InsetRight = FCString::Atof(*R);
			if (!T.IsEmpty()) BD.InsetTop = FCString::Atof(*T);
			if (!B.IsEmpty()) BD.InsetBottom = FCString::Atof(*B);
		}
	}

	return BD;
}

// ─── Set of recognized frame type tags ─────────────────────────────────────────

static bool IsFrameTypeTag(const FString& Tag)
{
	static const TSet<FString> FrameTags = {
		TEXT("Frame"), TEXT("Button"), TEXT("CheckButton"), TEXT("EditBox"),
		TEXT("ScrollFrame"), TEXT("ScrollingMessageFrame"), TEXT("MessageFrame"),
		TEXT("SimpleHTML"), TEXT("Slider"), TEXT("StatusBar"), TEXT("Cooldown"),
		TEXT("ColorSelect"), TEXT("GameTooltip"), TEXT("Minimap"),
		TEXT("Model"), TEXT("PlayerModel"), TEXT("DressUpModel"),
		TEXT("TabardModel"), TEXT("WorldFrame")
	};
	return FrameTags.Contains(Tag);
}

// ─── Forward declare ───────────────────────────────────────────────────────────

static FWowFrameDef ParseFrameNode(const FXmlNode* Node);

#if HAS_PUGIXML
static FWowFrameDef ParseFrameNode_Pugi(const pugi::xml_node& Node)
{
	FWowFrameDef Def;
	Def.Type = FWowFrameXmlParser::ParseFrameType(UTF8_TO_TCHAR(Node.name()));
	Def.Name = UTF8_TO_TCHAR(Node.attribute("name").as_string());
	Def.ParentKey = UTF8_TO_TCHAR(Node.attribute("parentKey").as_string());
	Def.Parent = UTF8_TO_TCHAR(Node.attribute("parent").as_string());
	Def.Inherits = UTF8_TO_TCHAR(Node.attribute("inherits").as_string());
	Def.bVirtual = Node.attribute("virtual").as_bool(false);
	Def.bHidden = Node.attribute("hidden").as_bool(false);
	Def.bSetAllPoints = Node.attribute("setAllPoints").as_bool(false);
	Def.bEnableMouse = Node.attribute("enableMouse").as_bool(false);
	Def.bEnableKeyboard = Node.attribute("enableKeyboard").as_bool(false);
	Def.bClampedToScreen = Node.attribute("clampedToScreen").as_bool(false);
	// <Button text="CLOSE" inherits="UIPanelButtonTemplate">: the label is the global string the text
	// attribute names (the client calls SetText with it). It was never read, so every such button was
	// blank - Dressing Room "Close"/"Reset" (Stefan 11.10). A <ButtonText> without text keeps it.
	Def.ButtonText = UTF8_TO_TCHAR(Node.attribute("text").as_string());

	const char* Strata = Node.attribute("frameStrata").as_string("");
	if (Strata[0]) Def.Strata = FWowFrameXmlParser::ParseStrata(UTF8_TO_TCHAR(Strata));

	Def.FrameLevel = Node.attribute("frameLevel").as_int(0);
	Def.FrameID = Node.attribute("id").as_int(0);
	Def.MinValue = Node.attribute("minValue").as_float(0.0f);
	Def.MaxValue = Node.attribute("maxValue").as_float(100.0f);
	Def.DefaultValue = Node.attribute("defaultValue").as_float(0.0f);
	Def.BarTexture = UTF8_TO_TCHAR(Node.child("BarTexture").attribute("file").as_string());
	if (const auto Hit = Node.child("HitRectInsets"))
    {
        auto Inset = Hit.child("AbsInset");
        if (!Inset) Inset = Hit;
        Def.HitRectInsets = FMargin(Inset.attribute("left").as_float(),Inset.attribute("top").as_float(),
            Inset.attribute("right").as_float(),Inset.attribute("bottom").as_float());
    }
	TArray<FWowTextureElement> InlineTextures;

	// Parse child elements
	for (pugi::xml_node Child = Node.first_child(); Child; Child = Child.next_sibling())
	{
		if (Child.type() != pugi::node_element) continue;
		FString Tag = UTF8_TO_TCHAR(Child.name());

		if (Tag == TEXT("Size"))
		{
			pugi::xml_node AbsDim = Child.child("AbsDimension");
			if (AbsDim)
			{
				Def.Width = AbsDim.attribute("x").as_float(0.0f);
				Def.Height = AbsDim.attribute("y").as_float(0.0f);
			}
			else
			{
				// Short form <Size x="30" y="30"/> (MultiCastSlotButtonTemplate and most 3.x XML).
				if (Child.attribute("x")) Def.Width = Child.attribute("x").as_float(0.0f);
				if (Child.attribute("y")) Def.Height = Child.attribute("y").as_float(0.0f);
			}
		}
		else if (Tag == TEXT("Anchors"))
		{
			for (pugi::xml_node Anchor = Child.child("Anchor"); Anchor; Anchor = Anchor.next_sibling("Anchor"))
			{
				FWowAnchor A;
				A.Point = FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(Anchor.attribute("point").as_string()));
				A.RelativeTo = UTF8_TO_TCHAR(Anchor.attribute("relativeTo").as_string());
				// WoW default: if relativePoint not specified, it equals point
				const char* RelPointStr = Anchor.attribute("relativePoint").as_string("");
				A.RelativePoint = (RelPointStr[0] != '\0')
					? FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(RelPointStr))
					: A.Point;
				pugi::xml_node Offset = Anchor.child("Offset");
				if (Offset)
				{
					// Handle nested: <Offset><AbsDimension x="..." y="..."/></Offset>
					pugi::xml_node OffAbsDim = Offset.child("AbsDimension");
					if (OffAbsDim)
					{
						A.OffsetX = OffAbsDim.attribute("x").as_float(0.0f);
						A.OffsetY = OffAbsDim.attribute("y").as_float(0.0f);
					}
					else
					{
						// Direct: <Offset x="..." y="..."/>
						A.OffsetX = Offset.attribute("x").as_float(0.0f);
						A.OffsetY = Offset.attribute("y").as_float(0.0f);
					}
				}
				else
				{
					// Direct AbsDimension: <AbsDimension x="..." y="..."/>
					pugi::xml_node DirectAbsDim = Anchor.child("AbsDimension");
					if (DirectAbsDim)
					{
						A.OffsetX = DirectAbsDim.attribute("x").as_float(0.0f);
						A.OffsetY = DirectAbsDim.attribute("y").as_float(0.0f);
					}
				}
				Def.Anchors.Add(A);
			}
		}
		else if (Tag == TEXT("Layers"))
		{
			for (pugi::xml_node Layer = Child.child("Layer"); Layer; Layer = Layer.next_sibling("Layer"))
			{
				FWowLayer L;
				L.Level = FWowFrameXmlParser::ParseDrawLayer(UTF8_TO_TCHAR(Layer.attribute("level").as_string("ARTWORK")));
				for (pugi::xml_node Tex = Layer.first_child(); Tex; Tex = Tex.next_sibling())
				{
					if (Tex.type() != pugi::node_element) continue;
					FString TexTag = UTF8_TO_TCHAR(Tex.name());
					if (TexTag == TEXT("Texture"))
					{
						FWowTextureElement T;
						T.Name = UTF8_TO_TCHAR(Tex.attribute("name").as_string());
						T.ParentKey = UTF8_TO_TCHAR(Tex.attribute("parentKey").as_string());
						T.File = UTF8_TO_TCHAR(Tex.attribute("file").as_string());
						T.bSetAllPoints = Tex.attribute("setAllPoints").as_bool(false);
						T.bHidden = Tex.attribute("hidden").as_bool(false);

						// Size
						pugi::xml_node TexSize = Tex.child("Size");
						if (TexSize)
						{
							pugi::xml_node TexAbsDim = TexSize.child("AbsDimension");
							if (TexAbsDim)
							{
								T.Width = TexAbsDim.attribute("x").as_float(0.0f);
								T.Height = TexAbsDim.attribute("y").as_float(0.0f);
							}
							else
							{
								T.Width = TexSize.attribute("x").as_float(0.0f);
								T.Height = TexSize.attribute("y").as_float(0.0f);
							}
						}

						// Anchors
						pugi::xml_node TexAnchors = Tex.child("Anchors");
						if (TexAnchors)
						{
							for (pugi::xml_node TexAnchor = TexAnchors.child("Anchor"); TexAnchor; TexAnchor = TexAnchor.next_sibling("Anchor"))
							{
								FWowAnchor A;
								A.Point = FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(TexAnchor.attribute("point").as_string()));
								A.RelativeTo = UTF8_TO_TCHAR(TexAnchor.attribute("relativeTo").as_string());
								const char* TexRelPoint = TexAnchor.attribute("relativePoint").as_string("");
								A.RelativePoint = (TexRelPoint[0] != '\0')
									? FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(TexRelPoint))
									: A.Point;
								pugi::xml_node TexOffset = TexAnchor.child("Offset");
								if (TexOffset)
								{
									pugi::xml_node TexOffAbsDim = TexOffset.child("AbsDimension");
									if (TexOffAbsDim)
									{
										A.OffsetX = TexOffAbsDim.attribute("x").as_float(0.0f);
										A.OffsetY = TexOffAbsDim.attribute("y").as_float(0.0f);
									}
									else
									{
										A.OffsetX = TexOffset.attribute("x").as_float(0.0f);
										A.OffsetY = TexOffset.attribute("y").as_float(0.0f);
									}
								}
								else
								{
									pugi::xml_node TexDirectAbsDim = TexAnchor.child("AbsDimension");
									if (TexDirectAbsDim)
									{
										A.OffsetX = TexDirectAbsDim.attribute("x").as_float(0.0f);
										A.OffsetY = TexDirectAbsDim.attribute("y").as_float(0.0f);
									}
								}
								T.Anchors.Add(A);
							}
						}

						// TexCoords
						pugi::xml_node TexCoords = Tex.child("TexCoords");
						if (TexCoords)
						{
							T.Left = TexCoords.attribute("left").as_float(0.0f);
							T.Right = TexCoords.attribute("right").as_float(1.0f);
							T.Top = TexCoords.attribute("top").as_float(0.0f);
							T.Bottom = TexCoords.attribute("bottom").as_float(1.0f);
						}

						// Color
						pugi::xml_node TexColor = Tex.child("Color");
						if (TexColor)
						{
							T.VertexColor = FLinearColor(
								TexColor.attribute("r").as_float(1.0f),
								TexColor.attribute("g").as_float(1.0f),
								TexColor.attribute("b").as_float(1.0f),
								TexColor.attribute("a").as_float(1.0f));
						}

						L.Textures.Add(T);
					}
					else if (TexTag == TEXT("FontString"))
					{
						FWowFontStringElement FS;
						FS.Name = UTF8_TO_TCHAR(Tex.attribute("name").as_string());
						FS.ParentKey = UTF8_TO_TCHAR(Tex.attribute("parentKey").as_string());
						FS.Text = UTF8_TO_TCHAR(Tex.attribute("text").as_string());
						FS.Inherits = UTF8_TO_TCHAR(Tex.attribute("inherits").as_string());
						// No justifyH: the font object decides (GameFontNormalLeft ... LEFT, otherwise CENTER as
						// in the client). Defaulting to LEFT put the Dressing Room title against the left edge
						// of its 300-unit box (Stefan 11.10).
						FS.JustifyH = UTF8_TO_TCHAR(Tex.attribute("justifyH").as_string(""));

						// Size
						pugi::xml_node FSSize = Tex.child("Size");
						if (FSSize)
						{
							pugi::xml_node FSAbsDim = FSSize.child("AbsDimension");
							if (FSAbsDim)
							{
								FS.Width = FSAbsDim.attribute("x").as_float(0.0f);
								FS.Height = FSAbsDim.attribute("y").as_float(0.0f);
							}
							else
							{
								FS.Width = FSSize.attribute("x").as_float(0.0f);
								FS.Height = FSSize.attribute("y").as_float(0.0f);
							}
						}

						// Anchors
						pugi::xml_node FSAnchors = Tex.child("Anchors");
						if (FSAnchors)
						{
							for (pugi::xml_node FSAnchor = FSAnchors.child("Anchor"); FSAnchor; FSAnchor = FSAnchor.next_sibling("Anchor"))
							{
								FWowAnchor A;
								A.Point = FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(FSAnchor.attribute("point").as_string()));
								A.RelativeTo = UTF8_TO_TCHAR(FSAnchor.attribute("relativeTo").as_string());
								const char* FSRelPoint = FSAnchor.attribute("relativePoint").as_string("");
								A.RelativePoint = (FSRelPoint[0] != '\0')
									? FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(FSRelPoint))
									: A.Point;
								pugi::xml_node FSOffset = FSAnchor.child("Offset");
								if (FSOffset)
								{
									pugi::xml_node FSOffAbsDim = FSOffset.child("AbsDimension");
									if (FSOffAbsDim)
									{
										A.OffsetX = FSOffAbsDim.attribute("x").as_float(0.0f);
										A.OffsetY = FSOffAbsDim.attribute("y").as_float(0.0f);
									}
									else
									{
										A.OffsetX = FSOffset.attribute("x").as_float(0.0f);
										A.OffsetY = FSOffset.attribute("y").as_float(0.0f);
									}
								}
								else
								{
									pugi::xml_node FSDirectAbsDim = FSAnchor.child("AbsDimension");
									if (FSDirectAbsDim)
									{
										A.OffsetX = FSDirectAbsDim.attribute("x").as_float(0.0f);
										A.OffsetY = FSDirectAbsDim.attribute("y").as_float(0.0f);
									}
								}
								FS.Anchors.Add(A);
							}
						}

						pugi::xml_node FH = Tex.child("FontHeight");
						if (FH)
						{
							pugi::xml_node AbsVal = FH.child("AbsValue");
							if (AbsVal) { FS.FontHeight = AbsVal.attribute("val").as_float(12.0f); FS.bHasFontHeight = true; }
						}
						pugi::xml_node Color = Tex.child("Color");
						if (Color)
						{
							FS.Color = FLinearColor(
								Color.attribute("r").as_float(1.0f),
								Color.attribute("g").as_float(1.0f),
								Color.attribute("b").as_float(1.0f),
								Color.attribute("a").as_float(1.0f));
							FS.bHasColor = true;
						}
						L.FontStrings.Add(FS);
					}
				}
				Def.Layers.Add(L);
			}
		}
		else if (Tag == TEXT("Attributes"))
		{
			for (pugi::xml_node Attr = Child.child("Attribute"); Attr; Attr = Attr.next_sibling("Attribute"))
				Def.XmlAttributes.Add({UTF8_TO_TCHAR(Attr.attribute("name").as_string()),
					UTF8_TO_TCHAR(Attr.attribute("type").as_string("string")), UTF8_TO_TCHAR(Attr.attribute("value").as_string())});
		}
		else if (Tag == TEXT("Scripts"))
		{
			for (pugi::xml_node Script = Child.first_child(); Script; Script = Script.next_sibling())
			{
				if (Script.type() != pugi::node_element) continue;
				FWowScriptHandler SH;
				SH.Event = UTF8_TO_TCHAR(Script.name());
				SH.Code = UTF8_TO_TCHAR(Script.child_value());
				SH.File = UTF8_TO_TCHAR(Script.attribute("function").as_string());
				if (SH.File.IsEmpty())
				{
					SH.File = UTF8_TO_TCHAR(Script.attribute("file").as_string());
				}
				if (!SH.Code.IsEmpty() || !SH.File.IsEmpty())
				{
					Def.Scripts.Add(SH);
				}
			}
		}
		else if (Tag == TEXT("Animations"))
		{
			CollectNamedObjectGlobals_Pugi(Child, Def.NamedObjectGlobals);
		}
		else if (Tag == TEXT("Frames"))
		{
			for (pugi::xml_node ChildFrame = Child.first_child(); ChildFrame; ChildFrame = ChildFrame.next_sibling())
			{
				if (ChildFrame.type() != pugi::node_element) continue;
				Def.Children.Add(ParseFrameNode_Pugi(ChildFrame));
			}
		}
		else if (Tag == TEXT("ScrollChild"))
		{
			for (pugi::xml_node ChildFrame = Child.first_child(); ChildFrame; ChildFrame = ChildFrame.next_sibling())
			{
				if (ChildFrame.type() != pugi::node_element) continue;
				if (IsFrameTypeTag(UTF8_TO_TCHAR(ChildFrame.name())))
				{
					Def.Children.Add(ParseFrameNode_Pugi(ChildFrame));
				}
			}
		}
		else if (Tag == TEXT("Backdrop"))
		{
			FWowBackdrop BD;
			BD.BgFile = UTF8_TO_TCHAR(Child.attribute("bgFile").as_string());
			BD.EdgeFile = UTF8_TO_TCHAR(Child.attribute("edgeFile").as_string());
			BD.Tile = Child.attribute("tile").as_bool(false);
			// r253 (Stefan 12.10: the Release Spirit dialog had no dialog border): FrameXML writes
			// <EdgeSize><AbsValue val="32"/></EdgeSize> and <BackgroundInsets><AbsInset .../>; only a
			// val attribute on EdgeSize itself was read, so XML dialog borders had size 0 and no insets.
			auto SizeValue = [](const pugi::xml_node& Node) -> int32
			{
				if (!Node) return 0;
				if (const pugi::xml_attribute Val = Node.attribute("val")) return Val.as_int(0);
				if (const pugi::xml_node Abs = Node.child("AbsValue")) return Abs.attribute("val").as_int(0);
				return 0;
			};
			if (const pugi::xml_attribute A = Child.attribute("edgeSize")) BD.EdgeSize = A.as_int(0);
			if (const pugi::xml_attribute A = Child.attribute("tileSize")) BD.TileSize = A.as_int(0);
			if (const int32 ES = SizeValue(Child.child("EdgeSize"))) BD.EdgeSize = ES;
			if (const int32 TS = SizeValue(Child.child("TileSize"))) BD.TileSize = TS;
			if (const pugi::xml_node Insets = Child.child("BackgroundInsets"))
			{
				const pugi::xml_node AbsInset = Insets.child("AbsInset") ? Insets.child("AbsInset") : Insets;
				BD.InsetLeft = AbsInset.attribute("left").as_float(BD.InsetLeft);
				BD.InsetRight = AbsInset.attribute("right").as_float(BD.InsetRight);
				BD.InsetTop = AbsInset.attribute("top").as_float(BD.InsetTop);
				BD.InsetBottom = AbsInset.attribute("bottom").as_float(BD.InsetBottom);
			}
			Def.Backdrop = BD;
		}
		else if (Tag == TEXT("NormalTexture"))
		{
			Def.NormalTexture = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			Def.NormalTextureName = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			Def.NormalTextureParentKey = UTF8_TO_TCHAR(Child.attribute("parentKey").as_string());
            const auto UV = Child.child("TexCoords");
            Def.NormalTextureUV = FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
                UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1));
            ResolveVirtualButtonTexture(Child, Def.NormalTexture, Def.NormalTextureUV);
            ReadButtonTextureLayout(Child, Def); // r379
		}
		else if (Tag == TEXT("PushedTexture"))
		{
			Def.PushedTexture = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			Def.PushedTextureName = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			Def.PushedTextureParentKey = UTF8_TO_TCHAR(Child.attribute("parentKey").as_string());
            const auto UV = Child.child("TexCoords");
            Def.PushedTextureUV = FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
                UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1));
            ResolveVirtualButtonTexture(Child, Def.PushedTexture, Def.PushedTextureUV);
		}
		else if (Tag == TEXT("HighlightTexture"))
		{
			Def.HighlightTexture = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			Def.HighlightTextureName = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			Def.HighlightTextureParentKey = UTF8_TO_TCHAR(Child.attribute("parentKey").as_string());
            const auto UV = Child.child("TexCoords");
            Def.HighlightTextureUV = FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
                UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1));
            ResolveVirtualButtonTexture(Child, Def.HighlightTexture, Def.HighlightTextureUV);
		}
		else if (Tag == TEXT("DisabledTexture"))
		{
			Def.DisabledTexture = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			Def.DisabledTextureName = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			Def.DisabledTextureParentKey = UTF8_TO_TCHAR(Child.attribute("parentKey").as_string());
            const auto UV = Child.child("TexCoords");
            Def.DisabledTextureUV = FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
                UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1));
            ResolveVirtualButtonTexture(Child, Def.DisabledTexture, Def.DisabledTextureUV);
		}
		else if (Tag == TEXT("CheckedTexture"))
		{
			Def.CheckedTexture = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			const auto UV = Child.child("TexCoords");
			Def.CheckedTextureUV = FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
				UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1));
			ResolveVirtualButtonTexture(Child, Def.CheckedTexture, Def.CheckedTextureUV);
		}
		else if (Tag == TEXT("ThumbTexture"))
		{
			FWowTextureElement ThumbTexture;
			ThumbTexture.Name = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			ThumbTexture.ParentKey = UTF8_TO_TCHAR(Child.attribute("parentKey").as_string());
			ThumbTexture.File = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			ThumbTexture.bSetAllPoints = Child.attribute("setAllPoints").as_bool(false);
			ThumbTexture.bHidden = Child.attribute("hidden").as_bool(false);

			pugi::xml_node ThumbSize = Child.child("Size");
			if (ThumbSize)
			{
				pugi::xml_node ThumbAbsDim = ThumbSize.child("AbsDimension");
				if (ThumbAbsDim)
				{
					ThumbTexture.Width = ThumbAbsDim.attribute("x").as_float(0.0f);
					ThumbTexture.Height = ThumbAbsDim.attribute("y").as_float(0.0f);
				}
			}

			pugi::xml_node ThumbAnchors = Child.child("Anchors");
			if (ThumbAnchors)
			{
				for (pugi::xml_node ThumbAnchor = ThumbAnchors.child("Anchor"); ThumbAnchor; ThumbAnchor = ThumbAnchor.next_sibling("Anchor"))
				{
					FWowAnchor A;
					A.Point = FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(ThumbAnchor.attribute("point").as_string()));
					A.RelativeTo = UTF8_TO_TCHAR(ThumbAnchor.attribute("relativeTo").as_string());
					const char* ThumbRelPoint = ThumbAnchor.attribute("relativePoint").as_string("");
					A.RelativePoint = (ThumbRelPoint[0] != '\0')
						? FWowFrameXmlParser::ParseAnchorPoint(UTF8_TO_TCHAR(ThumbRelPoint))
						: A.Point;
					pugi::xml_node ThumbOffset = ThumbAnchor.child("Offset");
					if (ThumbOffset)
					{
						pugi::xml_node ThumbOffAbsDim = ThumbOffset.child("AbsDimension");
						if (ThumbOffAbsDim)
						{
							A.OffsetX = ThumbOffAbsDim.attribute("x").as_float(0.0f);
							A.OffsetY = ThumbOffAbsDim.attribute("y").as_float(0.0f);
						}
						else
						{
							A.OffsetX = ThumbOffset.attribute("x").as_float(0.0f);
							A.OffsetY = ThumbOffset.attribute("y").as_float(0.0f);
						}
					}
					ThumbTexture.Anchors.Add(A);
				}
			}

			InlineTextures.Add(MoveTemp(ThumbTexture));
		}
		else if (Tag == TEXT("ButtonText"))
		{
			pugi::xml_node FS = Child.child("FontString");
			const FString ChildText = FS ? UTF8_TO_TCHAR(FS.attribute("text").as_string()) : UTF8_TO_TCHAR(Child.child_value());
			if (!ChildText.TrimStartAndEnd().IsEmpty() || Def.ButtonText.IsEmpty()) Def.ButtonText = ChildText;
			// <ButtonText name="$parentText"/> with no text still makes the label that
			// Lua fills later (UIPanelButtonTemplate: _G[name.."Text"]:SetText(...)).
			if (Def.ButtonText.IsEmpty()) Def.ButtonText = TEXT(" ");
		}
	}

	AppendInlineTexturesToLayers(Def, InlineTextures);

	return Def;
}
#endif

// ─── Parse a frame node recursively ────────────────────────────────────────────

static FWowFrameDef ParseFrameNode(const FXmlNode* Node)
{
	FWowFrameDef Def;
	Def.Type = FWowFrameXmlParser::ParseFrameType(Node->GetTag());
	Def.Name = Node->GetAttribute(TEXT("name"));
	Def.ParentKey = Node->GetAttribute(TEXT("parentKey"));
	Def.Parent = Node->GetAttribute(TEXT("parent"));
	Def.Inherits = Node->GetAttribute(TEXT("inherits"));
	TArray<FWowTextureElement> InlineTextures;

	FString Virtual = Node->GetAttribute(TEXT("virtual"));
	Def.bVirtual = Virtual.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	FString Hidden = Node->GetAttribute(TEXT("hidden"));
	Def.bHidden = Hidden.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	FString SetAllPoints = Node->GetAttribute(TEXT("setAllPoints"));
	Def.bSetAllPoints = SetAllPoints.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	FString EnableMouse = Node->GetAttribute(TEXT("enableMouse"));
	Def.bEnableMouse = EnableMouse.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	FString EnableKeyboard = Node->GetAttribute(TEXT("enableKeyboard"));
	Def.bEnableKeyboard = EnableKeyboard.Equals(TEXT("true"), ESearchCase::IgnoreCase);

	Def.bClampedToScreen = Node->GetAttribute(TEXT("clampedToScreen")).Equals(TEXT("true"), ESearchCase::IgnoreCase);
	Def.ButtonText = Node->GetAttribute(TEXT("text")); // <Button text="CLOSE">, as in the pugi parser

	FString StrataStr = Node->GetAttribute(TEXT("frameStrata"));
	if (!StrataStr.IsEmpty()) Def.Strata = FWowFrameXmlParser::ParseStrata(StrataStr);

	FString LevelStr = Node->GetAttribute(TEXT("frameLevel"));
	if (!LevelStr.IsEmpty()) Def.FrameLevel = FCString::Atoi(*LevelStr);

	FString IdStr = Node->GetAttribute(TEXT("id"));
	if (!IdStr.IsEmpty()) Def.FrameID = FCString::Atoi(*IdStr);

    if (const FXmlNode* Hit = Node->FindChildNode(TEXT("HitRectInsets")))
    {
        const FXmlNode* Inset = Hit->FindChildNode(TEXT("AbsInset"));
        if (!Inset) Inset = Hit;
        Def.HitRectInsets = FMargin(FCString::Atof(*Inset->GetAttribute(TEXT("left"))),
            FCString::Atof(*Inset->GetAttribute(TEXT("top"))),FCString::Atof(*Inset->GetAttribute(TEXT("right"))),
            FCString::Atof(*Inset->GetAttribute(TEXT("bottom"))));
    }
	// Type-specific attributes
	FString Orientation = Node->GetAttribute(TEXT("orientation"));
	if (!Orientation.IsEmpty()) Def.Orientation = Orientation;

	FString MinVal = Node->GetAttribute(TEXT("minValue"));
	FString MaxVal = Node->GetAttribute(TEXT("maxValue"));
	FString DefVal = Node->GetAttribute(TEXT("defaultValue"));
	if (!MinVal.IsEmpty()) Def.MinValue = FCString::Atof(*MinVal);
	if (!MaxVal.IsEmpty()) Def.MaxValue = FCString::Atof(*MaxVal);
	if (!DefVal.IsEmpty()) Def.DefaultValue = FCString::Atof(*DefVal);
	if (const FXmlNode* BarTexture = Node->FindChildNode(TEXT("BarTexture")))
	{
		Def.BarTexture = BarTexture->GetAttribute(TEXT("file"));
	}

	// Parse children
	for (const FXmlNode* Child = Node->GetFirstChildNode(); Child; Child = Child->GetNextNode())
	{
		FString Tag = Child->GetTag();

		if (Tag.Equals(TEXT("Size"), ESearchCase::IgnoreCase))
		{
			ParseSize(Child, Def.Width, Def.Height);
		}
		else if (Tag.Equals(TEXT("Anchors"), ESearchCase::IgnoreCase))
		{
			Def.Anchors = ParseAnchors(Child);
		}
		else if (Tag.Equals(TEXT("Layers"), ESearchCase::IgnoreCase))
		{
			Def.Layers = ParseLayers(Child);
		}
		else if (Tag.Equals(TEXT("Scripts"), ESearchCase::IgnoreCase))
		{
			Def.Scripts = ParseScripts(Child);
		}
		else if (Tag.Equals(TEXT("Attributes"), ESearchCase::IgnoreCase))
		{
			for (const FXmlNode* Attr = Child->GetFirstChildNode(); Attr; Attr = Attr->GetNextNode())
			{
				if (!Attr->GetTag().Equals(TEXT("Attribute"), ESearchCase::IgnoreCase)) continue;
				FString Type = Attr->GetAttribute(TEXT("type"));
				if (Type.IsEmpty()) Type = TEXT("string");
				Def.XmlAttributes.Add({Attr->GetAttribute(TEXT("name")), Type, Attr->GetAttribute(TEXT("value"))});
			}
		}
		else if (Tag.Equals(TEXT("Animations"), ESearchCase::IgnoreCase))
		{
			CollectNamedObjectGlobals(Child, Def.NamedObjectGlobals);
		}
		else if (Tag.Equals(TEXT("Backdrop"), ESearchCase::IgnoreCase))
		{
			Def.Backdrop = ParseBackdropNode(Child);
		}
		else if (Tag.Equals(TEXT("Frames"), ESearchCase::IgnoreCase))
		{
			for (const FXmlNode* ChildFrame = Child->GetFirstChildNode(); ChildFrame; ChildFrame = ChildFrame->GetNextNode())
			{
				if (IsFrameTypeTag(ChildFrame->GetTag()))
				{
					Def.Children.Add(ParseFrameNode(ChildFrame));
				}
			}
		}
		else if (Tag.Equals(TEXT("ScrollChild"), ESearchCase::IgnoreCase))
		{
			for (const FXmlNode* ChildFrame = Child->GetFirstChildNode(); ChildFrame; ChildFrame = ChildFrame->GetNextNode())
			{
				if (IsFrameTypeTag(ChildFrame->GetTag()))
				{
					Def.Children.Add(ParseFrameNode(ChildFrame));
				}
			}
		}
		else if (Tag.Equals(TEXT("ButtonText"), ESearchCase::IgnoreCase))
		{
			// ButtonText can have a FontString child or just text content
			const FXmlNode* FS = Child->FindChildNode(TEXT("FontString"));
			const FString ChildText = FS ? FS->GetAttribute(TEXT("text")) : Child->GetContent();
			if (!ChildText.TrimStartAndEnd().IsEmpty() || Def.ButtonText.IsEmpty()) Def.ButtonText = ChildText;
		}
		else if (Tag.Equals(TEXT("NormalTexture"), ESearchCase::IgnoreCase))
		{
			Def.NormalTexture = Child->GetAttribute(TEXT("file"));
			Def.NormalTextureName = Child->GetAttribute(TEXT("name"));
			Def.NormalTextureParentKey = Child->GetAttribute(TEXT("parentKey"));
            const FWowTextureElement Texture = ParseTextureNode(Child);
            Def.NormalTextureUV = FVector4(Texture.Left, Texture.Right, Texture.Top, Texture.Bottom);
            // r379: its own size and first anchor.
            Def.NormalTextureW = Texture.Width; Def.NormalTextureH = Texture.Height;
            if (Texture.Anchors.Num() > 0)
            {
                Def.NormalTexturePoint = Texture.Anchors[0].Point;
                Def.NormalTextureRelativePoint = Texture.Anchors[0].RelativePoint;
                Def.NormalTextureOffset = FVector2D(Texture.Anchors[0].OffsetX, Texture.Anchors[0].OffsetY);
            }
		}
		else if (Tag.Equals(TEXT("PushedTexture"), ESearchCase::IgnoreCase))
		{
			Def.PushedTexture = Child->GetAttribute(TEXT("file"));
			Def.PushedTextureName = Child->GetAttribute(TEXT("name"));
			Def.PushedTextureParentKey = Child->GetAttribute(TEXT("parentKey"));
            const FWowTextureElement Texture = ParseTextureNode(Child);
            Def.PushedTextureUV = FVector4(Texture.Left, Texture.Right, Texture.Top, Texture.Bottom);
		}
		else if (Tag.Equals(TEXT("HighlightTexture"), ESearchCase::IgnoreCase))
		{
			Def.HighlightTexture = Child->GetAttribute(TEXT("file"));
			Def.HighlightTextureName = Child->GetAttribute(TEXT("name"));
			Def.HighlightTextureParentKey = Child->GetAttribute(TEXT("parentKey"));
            const FWowTextureElement Texture = ParseTextureNode(Child);
            Def.HighlightTextureUV = FVector4(Texture.Left, Texture.Right, Texture.Top, Texture.Bottom);
		}
		else if (Tag.Equals(TEXT("DisabledTexture"), ESearchCase::IgnoreCase))
		{
			Def.DisabledTexture = Child->GetAttribute(TEXT("file"));
			Def.DisabledTextureName = Child->GetAttribute(TEXT("name"));
			Def.DisabledTextureParentKey = Child->GetAttribute(TEXT("parentKey"));
            const FWowTextureElement Texture = ParseTextureNode(Child);
            Def.DisabledTextureUV = FVector4(Texture.Left, Texture.Right, Texture.Top, Texture.Bottom);
		}
		else if (Tag.Equals(TEXT("ThumbTexture"), ESearchCase::IgnoreCase))
		{
			InlineTextures.Add(ParseTextureNode(Child));
		}
	}

	AppendInlineTexturesToLayers(Def, InlineTextures);

	return Def;
}

// ─── Public API ────────────────────────────────────────────────────────────────

TArray<FWowXmlDirective> FWowFrameXmlParser::ParseXml(const TArray<uint8>& Data, const FString& FileName)
{
	TArray<FWowXmlDirective> Directives;

	// Convert bytes to string
	FString XmlContent;
	if (Data.Num() >= 3 && Data[0] == 0xEF && Data[1] == 0xBB && Data[2] == 0xBF)
	{
		// UTF-8 BOM - skip it
		FUTF8ToTCHAR Conv((const ANSICHAR*)Data.GetData() + 3, Data.Num() - 3);
		XmlContent = FString(Conv.Length(), Conv.Get());
	}
	else
	{
		FUTF8ToTCHAR Conv((const ANSICHAR*)Data.GetData(), Data.Num());
		XmlContent = FString(Conv.Length(), Conv.Get());
	}

#if HAS_PUGIXML
	// Use pugixml for robust WoW XML parsing (handles namespaces, entities, comments, CDATA)
	pugi::xml_document Doc;
	constexpr unsigned int ParseFlags =
		pugi::parse_default |
		pugi::parse_comments |
		pugi::parse_declaration |
		pugi::parse_doctype |
		pugi::parse_pi;

	pugi::xml_parse_result ParseResult = Doc.load_buffer(Data.GetData(), Data.Num(), ParseFlags);

	if (!ParseResult)
	{
		FTCHARToUTF8 SanitizedUtf8(*XmlContent);
		ParseResult = Doc.load_buffer(
			SanitizedUtf8.Get(),
			SanitizedUtf8.Length(),
			ParseFlags,
			pugi::encoding_utf8);

		if (!ParseResult)
		{
			UE_LOG(LogWowXml, Warning, TEXT("Failed to parse XML with pugixml: %s - %hs (offset %d)"),
				*FileName, ParseResult.description(), static_cast<int32>(ParseResult.offset));
			return ParseXmlWithFXmlFallback(XmlContent, FileName);
		}
	}

	// Find the <Ui> root (may be nested under xml declaration)
	pugi::xml_node Root = Doc.child("Ui");
	if (!Root)
	{
		// Try first child element
		for (pugi::xml_node N = Doc.first_child(); N; N = N.next_sibling())
		{
			if (N.type() == pugi::node_element)
			{
				Root = N;
				break;
			}
		}
	}

	if (!Root)
	{
		UE_LOG(LogWowXml, Warning, TEXT("No root element in XML: %s"), *FileName);
		return Directives;
	}

	// Process children of root
	for (pugi::xml_node Child = Root.first_child(); Child; Child = Child.next_sibling())
	{
		if (Child.type() != pugi::node_element) continue;

		FString TagName = UTF8_TO_TCHAR(Child.name());

		if (TagName == TEXT("Script"))
		{
			FString File = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			if (!File.IsEmpty())
			{
				FWowXmlDirective Dir;
				Dir.Type = FWowXmlDirective::EType::Script;
				Dir.FilePath = File;
				Directives.Add(Dir);
			}
		}
		else if (TagName == TEXT("Include"))
		{
			FString File = UTF8_TO_TCHAR(Child.attribute("file").as_string());
			if (!File.IsEmpty())
			{
				FWowXmlDirective Dir;
				Dir.Type = FWowXmlDirective::EType::Include;
				Dir.FilePath = File;
				Directives.Add(Dir);
			}
		}
		else if (TagName == TEXT("Font"))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Font;
			Dir.FontName = UTF8_TO_TCHAR(Child.attribute("name").as_string());
			Dir.FontInherits = UTF8_TO_TCHAR(Child.attribute("inherits").as_string());
			Dir.FontFlags = UTF8_TO_TCHAR(Child.attribute("flags").as_string());
			Dir.FontHeight = Child.attribute("height").as_float(12.0f);
			Dir.bFontHasHeight = !Child.attribute("height").empty();
			Dir.FontFile = UTF8_TO_TCHAR(Child.attribute("font").as_string());

			pugi::xml_node FontHeightNode = Child.child("FontHeight");
			if (FontHeightNode)
			{
				pugi::xml_node AbsValue = FontHeightNode.child("AbsValue");
				if (AbsValue)
				{
					Dir.FontHeight = AbsValue.attribute("val").as_float(Dir.FontHeight);
					Dir.bFontHasHeight = true;
				}
			}
			if (pugi::xml_node ColorNode = Child.child("Color"))
			{
				Dir.bFontHasColor = true;
				Dir.FontColor = FLinearColor(ColorNode.attribute("r").as_float(1.f), ColorNode.attribute("g").as_float(1.f),
					ColorNode.attribute("b").as_float(1.f), ColorNode.attribute("a").as_float(1.f));
			}

			Directives.Add(Dir);
		}
		else
		{
			// <Texture name="UIPanelButtonUpTexture" virtual="true">: button state textures
			// inherit these (<NormalTexture inherits="UIPanelButtonUpTexture"/>).
			if (TagName == TEXT("Texture") && Child.attribute("virtual").as_bool(false))
			{
				const pugi::xml_node UV = Child.child("TexCoords");
				WowVirtualButtonTextures().Add(UTF8_TO_TCHAR(Child.attribute("name").as_string()),
					TPair<FString, FVector4>(UTF8_TO_TCHAR(Child.attribute("file").as_string()),
						FVector4(UV.attribute("left").as_float(0), UV.attribute("right").as_float(1),
							UV.attribute("top").as_float(0), UV.attribute("bottom").as_float(1))));
			}
			// Frame definition — parse it
			FWowFrameDef Def = ParseFrameNode_Pugi(Child);
			if (!Def.Name.IsEmpty() || Def.Type != EWowFrameType::Frame)
			{
				FWowXmlDirective Dir;
				Dir.Type = FWowXmlDirective::EType::Frame;
				Dir.FrameDef = MoveTemp(Def);
				Directives.Add(Dir);
			}
		}
	}

	UE_LOG(LogWowXml, Log, TEXT("Parsed XML: %s (%d directives)"), *FileName, Directives.Num());

#else
	Directives = ParseXmlWithFXmlFallback(XmlContent, FileName);
#endif // !HAS_PUGIXML fallback end

	return Directives;
}

TArray<FWowXmlDirective> FWowFrameXmlParser::LoadFrameXml(FMpqManager* Mpq)
{
	TArray<FWowXmlDirective> AllDirectives;

	if (!Mpq)
	{
		UE_LOG(LogWowXml, Error, TEXT("LoadFrameXml: No MPQ manager"));
		return AllDirectives;
	}

	TArray<uint8> TocData;
	const FString FrameXmlTocPath = TEXT("Interface\\FrameXML\\FrameXML.toc");
	if (!TryReadWowFile(Mpq, FrameXmlTocPath, TocData))
	{
		UE_LOG(LogWowXml, Error, TEXT("Failed to read %s"), *FrameXmlTocPath);
		return AllDirectives;
	}

	FString TocContent;
	if (TocData.Num() >= 3 && TocData[0] == 0xEF && TocData[1] == 0xBB && TocData[2] == 0xBF)
	{
		FUTF8ToTCHAR Conv((const ANSICHAR*)TocData.GetData() + 3, TocData.Num() - 3);
		TocContent = FString(Conv.Length(), Conv.Get());
	}
	else
	{
		FUTF8ToTCHAR Conv((const ANSICHAR*)TocData.GetData(), TocData.Num());
		TocContent = FString(Conv.Length(), Conv.Get());
	}

	TArray<FString> Lines;
	TocContent.ParseIntoArrayLines(Lines);
	TSet<FString> VisitedXmlFiles;
	const FString FrameXmlBaseDirectory = TEXT("Interface\\FrameXML\\");

	for (const FString& Line : Lines)
	{
		FString Trimmed = Line.TrimStartAndEnd();

		if (Trimmed.IsEmpty() || Trimmed.StartsWith(TEXT("##")) || Trimmed.StartsWith(TEXT("#")))
		{
			continue;
		}

		Trimmed.ReplaceInline(TEXT("/"), TEXT("\\"));

		if (Trimmed.EndsWith(TEXT(".xml"), ESearchCase::IgnoreCase))
		{
			AppendResolvedXmlDirectives(Mpq, ResolveWowPath(FrameXmlBaseDirectory, Trimmed), VisitedXmlFiles, AllDirectives);
		}
		else if (Trimmed.EndsWith(TEXT(".lua"), ESearchCase::IgnoreCase))
		{
			FWowXmlDirective Dir;
			Dir.Type = FWowXmlDirective::EType::Script;
			Dir.FilePath = ResolveWowPath(FrameXmlBaseDirectory, Trimmed);
			AllDirectives.Add(MoveTemp(Dir));
		}
	}

	UE_LOG(LogWowXml, Log, TEXT("LoadFrameXml: %d total directives from FrameXML.toc"), AllDirectives.Num());
	return AllDirectives;
}
