#include "UnrealBridgeRegistryLibrary.h"
#include "UnrealBridgeVersion.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/EngineVersion.h"
#include "Misc/SecureHash.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/FieldIterator.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

namespace BridgeRegistry
{
	FString ToPythonName(const FString& NativeName)
	{
		FString Result;
		Result.Reserve(NativeName.Len() + 8);
		for (int32 Index = 0; Index < NativeName.Len(); ++Index)
		{
			const TCHAR Ch = NativeName[Index];
			const bool bUpper = FChar::IsUpper(Ch);
			if (bUpper && Index > 0)
			{
				const TCHAR Prev = NativeName[Index - 1];
				const bool bPrevLowerOrDigit = FChar::IsLower(Prev) || FChar::IsDigit(Prev);
				const bool bNextLower = Index + 1 < NativeName.Len() && FChar::IsLower(NativeName[Index + 1]);
				if ((bPrevLowerOrDigit || (FChar::IsUpper(Prev) && bNextLower)) && !Result.EndsWith(TEXT("_")))
				{
					Result.AppendChar(TEXT('_'));
				}
			}
			Result.AppendChar(FChar::ToLower(Ch));
		}
		return Result;
	}

	TArray<TSharedPtr<FJsonValue>> EnumValues(const UEnum* Enum)
	{
		TArray<TSharedPtr<FJsonValue>> Values;
		if (!Enum)
		{
			return Values;
		}
		for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
		{
			if (Enum->HasMetaData(TEXT("Hidden"), Index))
			{
				continue;
			}
			const FString Name = Enum->GetNameStringByIndex(Index);
			if (!Name.EndsWith(TEXT("_MAX")) && Name != TEXT("MAX"))
			{
				Values.Add(MakeShared<FJsonValueString>(Name));
			}
		}
		return Values;
	}

	TSharedRef<FJsonObject> DescribeProperty(const FProperty* Property, int32 Depth = 0)
	{
		TSharedRef<FJsonObject> Description = MakeShared<FJsonObject>();
		Description->SetStringField(TEXT("name"), ToPythonName(Property->GetName()));
		Description->SetStringField(TEXT("native_name"), Property->GetName());
		Description->SetStringField(TEXT("cpp_type"), Property->GetCPPType());

		TSharedRef<FJsonObject> Schema = MakeShared<FJsonObject>();
		FString PythonType = TEXT("Any");
		FString Kind = TEXT("unknown");

		if (CastField<FBoolProperty>(Property))
		{
			PythonType = TEXT("bool");
			Kind = TEXT("boolean");
			Schema->SetStringField(TEXT("type"), TEXT("boolean"));
		}
		else if (CastField<FIntProperty>(Property) || CastField<FInt64Property>(Property)
			|| CastField<FInt16Property>(Property) || CastField<FInt8Property>(Property)
			|| CastField<FUInt32Property>(Property) || CastField<FUInt64Property>(Property)
			|| CastField<FUInt16Property>(Property))
		{
			PythonType = TEXT("int");
			Kind = TEXT("integer");
			Schema->SetStringField(TEXT("type"), TEXT("integer"));
		}
		else if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			const UEnum* Enum = EnumProperty->GetEnum();
			PythonType = Enum ? Enum->GetName() : TEXT("int");
			Kind = TEXT("enum");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			Schema->SetArrayField(TEXT("enum"), EnumValues(Enum));
			if (Enum)
			{
				Schema->SetStringField(TEXT("x-unreal-enum"), Enum->GetPathName());
			}
		}
		else if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (ByteProperty->Enum)
			{
				PythonType = ByteProperty->Enum->GetName();
				Kind = TEXT("enum");
				Schema->SetStringField(TEXT("type"), TEXT("string"));
				Schema->SetArrayField(TEXT("enum"), EnumValues(ByteProperty->Enum));
				Schema->SetStringField(TEXT("x-unreal-enum"), ByteProperty->Enum->GetPathName());
			}
			else
			{
				PythonType = TEXT("int");
				Kind = TEXT("integer");
				Schema->SetStringField(TEXT("type"), TEXT("integer"));
				Schema->SetNumberField(TEXT("minimum"), 0);
				Schema->SetNumberField(TEXT("maximum"), 255);
			}
		}
		else if (CastField<FFloatProperty>(Property) || CastField<FDoubleProperty>(Property))
		{
			PythonType = TEXT("float");
			Kind = TEXT("number");
			Schema->SetStringField(TEXT("type"), TEXT("number"));
		}
		else if (CastField<FStrProperty>(Property) || CastField<FNameProperty>(Property)
			|| CastField<FTextProperty>(Property))
		{
			PythonType = TEXT("str");
			Kind = TEXT("string");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			if (CastField<FNameProperty>(Property))
			{
				Schema->SetStringField(TEXT("x-unreal-type"), TEXT("Name"));
			}
			else if (CastField<FTextProperty>(Property))
			{
				Schema->SetStringField(TEXT("x-unreal-type"), TEXT("Text"));
			}
		}
		else if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
		{
			const FString ClassName = ClassProperty->MetaClass ? ClassProperty->MetaClass->GetName() : TEXT("Object");
			PythonType = FString::Printf(TEXT("type[%s]"), *ClassName);
			Kind = TEXT("class");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			Schema->SetStringField(TEXT("format"), TEXT("unreal-class-path"));
			Schema->SetStringField(TEXT("x-unreal-class"), ClassName);
		}
		else if (const FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
		{
			const FString ClassName = SoftClassProperty->MetaClass ? SoftClassProperty->MetaClass->GetName() : TEXT("Object");
			PythonType = FString::Printf(TEXT("SoftClass[%s]"), *ClassName);
			Kind = TEXT("soft_class");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			Schema->SetStringField(TEXT("format"), TEXT("unreal-class-path"));
			Schema->SetStringField(TEXT("x-unreal-class"), ClassName);
		}
		else if (const FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
		{
			const FString ClassName = SoftObjectProperty->PropertyClass ? SoftObjectProperty->PropertyClass->GetName() : TEXT("Object");
			PythonType = FString::Printf(TEXT("SoftObject[%s]"), *ClassName);
			Kind = TEXT("soft_object");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			Schema->SetStringField(TEXT("format"), TEXT("unreal-object-path"));
			Schema->SetStringField(TEXT("x-unreal-class"), ClassName);
		}
		else if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			const FString ClassName = ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetName() : TEXT("Object");
			PythonType = ClassName;
			Kind = TEXT("object");
			Schema->SetStringField(TEXT("type"), TEXT("string"));
			Schema->SetStringField(TEXT("format"), TEXT("unreal-object-path"));
			Schema->SetStringField(TEXT("x-unreal-class"), ClassName);
		}
		else if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			const FString StructName = StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("Struct");
			PythonType = StructName;
			Kind = TEXT("struct");
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			Schema->SetStringField(TEXT("x-unreal-struct"), StructName);
		}
		else if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			TSharedRef<FJsonObject> Inner = Depth < 8
				? DescribeProperty(ArrayProperty->Inner, Depth + 1)
				: MakeShared<FJsonObject>();
			FString InnerType;
			Inner->TryGetStringField(TEXT("python_type"), InnerType);
			PythonType = FString::Printf(TEXT("list[%s]"), InnerType.IsEmpty() ? TEXT("Any") : *InnerType);
			Kind = TEXT("array");
			Schema->SetStringField(TEXT("type"), TEXT("array"));
			const TSharedPtr<FJsonObject>* InnerSchema = nullptr;
			if (Inner->TryGetObjectField(TEXT("json_schema"), InnerSchema) && InnerSchema)
			{
				Schema->SetObjectField(TEXT("items"), (*InnerSchema).ToSharedRef());
			}
		}
		else if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			TSharedRef<FJsonObject> Inner = Depth < 8
				? DescribeProperty(SetProperty->ElementProp, Depth + 1)
				: MakeShared<FJsonObject>();
			FString InnerType;
			Inner->TryGetStringField(TEXT("python_type"), InnerType);
			PythonType = FString::Printf(TEXT("set[%s]"), InnerType.IsEmpty() ? TEXT("Any") : *InnerType);
			Kind = TEXT("set");
			Schema->SetStringField(TEXT("type"), TEXT("array"));
			Schema->SetBoolField(TEXT("uniqueItems"), true);
			const TSharedPtr<FJsonObject>* InnerSchema = nullptr;
			if (Inner->TryGetObjectField(TEXT("json_schema"), InnerSchema) && InnerSchema)
			{
				Schema->SetObjectField(TEXT("items"), (*InnerSchema).ToSharedRef());
			}
		}
		else if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			TSharedRef<FJsonObject> Key = DescribeProperty(MapProperty->KeyProp, Depth + 1);
			TSharedRef<FJsonObject> Value = DescribeProperty(MapProperty->ValueProp, Depth + 1);
			FString KeyType;
			FString ValueType;
			Key->TryGetStringField(TEXT("python_type"), KeyType);
			Value->TryGetStringField(TEXT("python_type"), ValueType);
			PythonType = FString::Printf(TEXT("dict[%s, %s]"),
				KeyType.IsEmpty() ? TEXT("Any") : *KeyType,
				ValueType.IsEmpty() ? TEXT("Any") : *ValueType);
			Kind = TEXT("map");
			Schema->SetStringField(TEXT("type"), TEXT("object"));
			const TSharedPtr<FJsonObject>* ValueSchema = nullptr;
			if (Value->TryGetObjectField(TEXT("json_schema"), ValueSchema) && ValueSchema)
			{
				Schema->SetObjectField(TEXT("additionalProperties"), (*ValueSchema).ToSharedRef());
			}
		}
		else
		{
			Schema->SetArrayField(TEXT("type"), {
				MakeShared<FJsonValueString>(TEXT("object")),
				MakeShared<FJsonValueString>(TEXT("string")),
				MakeShared<FJsonValueString>(TEXT("null")),
			});
		}

		Description->SetStringField(TEXT("kind"), Kind);
		Description->SetStringField(TEXT("python_type"), PythonType);
		Description->SetObjectField(TEXT("json_schema"), Schema);
		return Description;
	}

	FString InferRisk(const UFunction* Function)
	{
		const FString Explicit = Function->GetMetaData(TEXT("ToolRisk"));
		if (!Explicit.IsEmpty())
		{
			return Explicit;
		}
		const FString Name = ToPythonName(Function->GetName());
		if (Name.Contains(TEXT("python")) || Name.StartsWith(TEXT("execute_console")))
		{
			return TEXT("Unsafe");
		}
		if (Name.StartsWith(TEXT("delete_")) || Name.StartsWith(TEXT("destroy_")))
		{
			return TEXT("Destructive");
		}
		if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure | FUNC_Const)
			|| Name.StartsWith(TEXT("get_")) || Name.StartsWith(TEXT("list_"))
			|| Name.StartsWith(TEXT("find_")) || Name.StartsWith(TEXT("query_"))
			|| Name.StartsWith(TEXT("search_")) || Name.StartsWith(TEXT("is_"))
			|| Name.StartsWith(TEXT("has_")) || Name.StartsWith(TEXT("can_"))
			|| Name.StartsWith(TEXT("validate_")) || Name.StartsWith(TEXT("analyze_"))
			|| Name.StartsWith(TEXT("describe_")))
		{
			return TEXT("ReadOnly");
		}
		return TEXT("Mutating");
	}

	FString MetadataOr(const UFunction* Function, const TCHAR* Key, const TCHAR* DefaultValue)
	{
		const FString Value = Function->GetMetaData(Key);
		return Value.IsEmpty() ? FString(DefaultValue) : Value;
	}

	TSharedRef<FJsonObject> DescribeFunction(const UFunction* Function)
	{
		TSharedRef<FJsonObject> Description = MakeShared<FJsonObject>();
		Description->SetStringField(TEXT("name"), ToPythonName(Function->GetName()));
		Description->SetStringField(TEXT("native_name"), Function->GetName());
		Description->SetStringField(TEXT("description"), Function->GetToolTipText().ToString());
		Description->SetStringField(TEXT("risk"), InferRisk(Function));
		Description->SetStringField(TEXT("execution"),
			MetadataOr(Function, TEXT("ToolExecution"), TEXT("GameThreadShort")));
		Description->SetStringField(TEXT("save_behavior"),
			MetadataOr(Function, TEXT("ToolSaveBehavior"), TEXT("Never")));
		Description->SetStringField(TEXT("provider"),
			MetadataOr(Function, TEXT("ToolProvider"), TEXT("UnrealBridge")));
		Description->SetStringField(TEXT("engine_min"),
			MetadataOr(Function, TEXT("ToolEngineMin"), UnrealBridgeVersion::MinimumEngine));
		Description->SetBoolField(TEXT("supports_dry_run"),
			Function->GetBoolMetaData(TEXT("ToolSupportsDryRun")));
		Description->SetBoolField(TEXT("supports_idempotency"),
			Function->GetBoolMetaData(TEXT("ToolSupportsIdempotency")));
		Description->SetStringField(TEXT("introduced_version"),
			MetadataOr(Function, TEXT("ToolIntroducedVersion"), TEXT("2.0.0")));

		TArray<TSharedPtr<FJsonValue>> Inputs;
		TArray<TSharedPtr<FJsonValue>> Outputs;
		TSharedRef<FJsonObject> InputProperties = MakeShared<FJsonObject>();
		TArray<TSharedPtr<FJsonValue>> Required;
		TArray<TSharedPtr<FJsonValue>> OutputSchemas;

		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Parm))
			{
				continue;
			}

			TSharedRef<FJsonObject> PropertyJson = DescribeProperty(Property);
			const bool bReturn = Property->HasAnyPropertyFlags(CPF_ReturnParm);
			const bool bOutOnly = !bReturn
				&& Property->HasAnyPropertyFlags(CPF_OutParm)
				&& !Property->HasAnyPropertyFlags(CPF_ReferenceParm | CPF_ConstParm);
			if (bReturn || bOutOnly)
			{
				PropertyJson->SetBoolField(TEXT("is_return"), bReturn);
				Outputs.Add(MakeShared<FJsonValueObject>(PropertyJson));
				const TSharedPtr<FJsonObject>* Schema = nullptr;
				if (PropertyJson->TryGetObjectField(TEXT("json_schema"), Schema) && Schema)
				{
					OutputSchemas.Add(MakeShared<FJsonValueObject>(*Schema));
				}
				continue;
			}

			const FString PythonName = ToPythonName(Property->GetName());
			const FString DefaultKey = FString::Printf(TEXT("CPP_Default_%s"), *Property->GetName());
			const FString DefaultValue = Function->GetMetaData(*DefaultKey);
			const bool bHasDefault = !DefaultValue.IsEmpty();
			PropertyJson->SetBoolField(TEXT("has_default"), bHasDefault);
			if (bHasDefault)
			{
				PropertyJson->SetStringField(TEXT("default"), DefaultValue);
			}
			Inputs.Add(MakeShared<FJsonValueObject>(PropertyJson));

			const TSharedPtr<FJsonObject>* Schema = nullptr;
			if (PropertyJson->TryGetObjectField(TEXT("json_schema"), Schema) && Schema)
			{
				InputProperties->SetObjectField(PythonName, (*Schema).ToSharedRef());
			}
			if (!bHasDefault)
			{
				Required.Add(MakeShared<FJsonValueString>(PythonName));
			}
		}

		TSharedRef<FJsonObject> InputSchema = MakeShared<FJsonObject>();
		InputSchema->SetStringField(TEXT("type"), TEXT("object"));
		InputSchema->SetObjectField(TEXT("properties"), InputProperties);
		InputSchema->SetArrayField(TEXT("required"), Required);
		InputSchema->SetBoolField(TEXT("additionalProperties"), false);
		Description->SetArrayField(TEXT("inputs"), Inputs);
		Description->SetArrayField(TEXT("outputs"), Outputs);
		Description->SetObjectField(TEXT("input_schema"), InputSchema);
		if (OutputSchemas.Num() == 1)
		{
			Description->SetObjectField(TEXT("output_schema"), OutputSchemas[0]->AsObject().ToSharedRef());
		}
		else
		{
			TSharedRef<FJsonObject> OutputSchema = MakeShared<FJsonObject>();
			OutputSchema->SetStringField(TEXT("type"), TEXT("array"));
			OutputSchema->SetArrayField(TEXT("prefixItems"), OutputSchemas);
			Description->SetObjectField(TEXT("output_schema"), OutputSchema);
		}
		return Description;
	}

	FString BuildRegistryJson()
	{
		TArray<UClass*> LibraryClasses;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (!Class || !Class->IsChildOf(UBlueprintFunctionLibrary::StaticClass()))
			{
				continue;
			}
			const FString Name = Class->GetName();
			if (Name.StartsWith(TEXT("UnrealBridge")) && Name.EndsWith(TEXT("Library")))
			{
				LibraryClasses.Add(Class);
			}
		}
		LibraryClasses.Sort([](const UClass& A, const UClass& B)
		{
			return A.GetName() < B.GetName();
		});

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetNumberField(TEXT("registry_version"), 1);
		Root->SetNumberField(TEXT("protocol_version"), 2);
		Root->SetStringField(TEXT("plugin_version"), UnrealBridgeVersion::Plugin);
		Root->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());

		TSharedRef<FJsonObject> Libraries = MakeShared<FJsonObject>();
		for (const UClass* Class : LibraryClasses)
		{
			TArray<const UFunction*> Functions;
			for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				const UFunction* Function = *It;
				if (Function && Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure))
				{
					Functions.Add(Function);
				}
			}
			Functions.Sort([](const UFunction& A, const UFunction& B)
			{
				return A.GetName() < B.GetName();
			});

			TSharedRef<FJsonObject> LibraryJson = MakeShared<FJsonObject>();
			TSharedRef<FJsonObject> FunctionsJson = MakeShared<FJsonObject>();
			for (const UFunction* Function : Functions)
			{
				FunctionsJson->SetObjectField(ToPythonName(Function->GetName()), DescribeFunction(Function));
			}
			LibraryJson->SetObjectField(TEXT("functions"), FunctionsJson);
			Libraries->SetObjectField(Class->GetName(), LibraryJson);
		}
		Root->SetObjectField(TEXT("libraries"), Libraries);

		FString Json;
		TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		FJsonSerializer::Serialize(Root, Writer);
		return Json;
	}
}

FString UUnrealBridgeRegistryLibrary::GetToolRegistryJson()
{
	return BridgeRegistry::BuildRegistryJson();
}

FString UUnrealBridgeRegistryLibrary::GetToolRegistryHash()
{
	return FMD5::HashAnsiString(*BridgeRegistry::BuildRegistryJson());
}
