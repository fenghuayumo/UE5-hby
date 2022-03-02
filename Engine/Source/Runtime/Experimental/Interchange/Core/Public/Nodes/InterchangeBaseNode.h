// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Nodes/InterchangeBaseNodeUtilities.h"
#include "Types/AttributeStorage.h"
#include "UObject/Class.h"
#include "UObject/Object.h"
#include "UObject/ObjectMacros.h"

#include "InterchangeBaseNode.generated.h"

/**
 * Internal Helper to get set custom property for class that derive from UInterchangeBaseNode. This is use by the macro IMPLEMENT_UOD_ATTRIBUTE.
 */
namespace InterchangePrivateNodeBase
{
	/**
	 * Retrieve a custom attribute if the attribute exist
	 *
	 * @param Attributes - The attribute storage you want to query the custom attribute
	 * @param AttributeKey - The storage key for the attribute
	 * @param OperationName - The name of the operation in case there is an error
	 * @param OutAttributeValue - This is where we store the value we retrieve from the storage
	 *
	 * @return - return true if the attribute exist in the storage and was query without error.
	 *           return false if the attribute do not exist or there is an error retriving it from the Storage
	 */
	template<typename ValueType>
	bool GetCustomAttribute(const UE::Interchange::FAttributeStorage& Attributes, const UE::Interchange::FAttributeKey& AttributeKey, const FString& OperationName, ValueType& OutAttributeValue)
	{
		if (!Attributes.ContainAttribute(AttributeKey))
		{
			return false;
		}
		UE::Interchange::FAttributeStorage::TAttributeHandle<ValueType> AttributeHandle = Attributes.GetAttributeHandle<ValueType>(AttributeKey);
		if (!AttributeHandle.IsValid())
		{
			return false;
		}
		UE::Interchange::EAttributeStorageResult Result = AttributeHandle.Get(OutAttributeValue);
		if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
			return false;
		}
		return true;
	}

	/**
	 * Add or update a custom attribute value in the specified storage
	 *
	 * @param Attributes - The attribute storage you want to add or update the custom attribute
	 * @param AttributeKey - The storage key for the attribute
	 * @param OperationName - The name of the operation in case there is an error
	 * @param AttributeValue - The value we want to add or update in the storage
	 */
	template<typename ValueType>
	bool SetCustomAttribute(UE::Interchange::FAttributeStorage& Attributes, const UE::Interchange::FAttributeKey& AttributeKey, const FString& OperationName, const ValueType& AttributeValue)
	{
		UE::Interchange::EAttributeStorageResult Result = Attributes.RegisterAttribute(AttributeKey, AttributeValue);
		if (!UE::Interchange::IsAttributeStorageResultSuccess(Result))
		{
			UE::Interchange::LogAttributeStorageErrors(Result, OperationName, AttributeKey);
			return false;
		}
		return true;
	}

	/**
	 * Finds a property by name in Outer and supports looking into FStructProperties (embedded structs) with a '.' separating the property names.
	 * 
	 * @param Container - The container for the property values. If the final property is inside a UScriptStruct, the container will be set to the UScriptStruct instance address.
	 * @param Outer - The UStruct containing the top level property.
	 * @param PropertyPath - A dot separated chain of properties. Doesn't support going through external objects.
	 * @return The Property matching the last name in the PropertyPath.
	 */
	INTERCHANGECORE_API FProperty* FindPropertyByPathChecked(TVariant<UObject*, uint8*>& Container, UStruct* Outer, FStringView PropertyPath);
}

/**
 * Use those macro to create Get/Set/ApplyToAsset for an attribute you want to support in your custom UOD node that derive from UInterchangeBaseNode.
 * The attribute key will be declare under private access modifier and the functions are declare under public access modifier.
 * So the class access modifier is public after calling this macro.
 *
 * @note - The Get will return false if the attribute was never set.
 * @note - The Set will add the attribute to the storage or update the value if the storage already has this attribute.
 * @note - The Apply will set a member variable of the AssetType instance. It will apply the value only if the storage contain the attribute key
 *
 * @param AttributeName - The name of the Get/Set functions. Example if you pass Foo you will end up with GetFoo and SetFoo function
 * @param AttributeType - This is to specify the type of the attribute. bool, float, FString... anything supported by the FAttributeStorage
 * @param AssetType - This is the asset you want to apply the storage value"
 * @param EnumType - Optional, specify it only if the AssetType member is an enum so we can type cast it in the apply function (we use uint8 to store the enum value)"
 */
#define IMPLEMENT_NODE_ATTRIBUTE_KEY(AttributeName)																\
const UE::Interchange::FAttributeKey Macro_Custom##AttributeName##Key = UE::Interchange::FAttributeKey(TEXT(#AttributeName));

#if WITH_ENGINE
#define IMPLEMENT_NODE_ATTRIBUTE_DELEGATE_BY_PROPERTYNAME(AttributeName, AttributeType, ObjectType, PropertyName)		\
bool ApplyCustom##AttributeName##ToAsset(UObject* Asset) const														\
{																													\
	return ApplyAttributeToObject<AttributeType>(Macro_Custom##AttributeName##Key.ToString(), Asset, PropertyName);	\
}																													\
																													\
bool FillCustom##AttributeName##FromAsset(UObject* Asset)															\
{																													\
	return FillAttributeFromObject<AttributeType>(Macro_Custom##AttributeName##Key.ToString(), Asset, PropertyName);\
}

#else //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_DELEGATE_BY_PROPERTYNAME(AttributeName, AttributeType, AssetType, PropertyName)

#endif //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_GETTER(AttributeName, AttributeType)																					\
	FString OperationName = GetTypeName() + TEXT(".Get" #AttributeName);																				\
	return InterchangePrivateNodeBase::GetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER_NODELEGATE(AttributeName, AttributeType)																		\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																				\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)														\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																			\
	if(InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue))	\
	{																																				\
		if(bAddApplyDelegate)																														\
		{																																			\
			AddApplyAndFillDelegates<AttributeType>(Macro_Custom##AttributeName##Key.ToString(), AssetType::StaticClass(), *Macro_Custom##AttributeName##Key.ToString()); \
		}																																			\
		return true;																																\
	}																																				\
	return false;

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER_WITH_CUSTOM_DELEGATE(NodeClassName, AttributeName, AttributeType, AssetType)								\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																			\
	if(InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue))	\
	{																																				\
		if(bAddApplyDelegate)																														\
		{																																			\
			TArray<UE::Interchange::FApplyAttributeToAsset>& Delegates = ApplyCustomAttributeDelegates.FindOrAdd(AssetType::StaticClass());			\
			Delegates.Add(UE::Interchange::FApplyAttributeToAsset::CreateUObject(this, &NodeClassName::ApplyCustom##AttributeName##ToAsset));		\
			TArray<UE::Interchange::FFillAttributeToAsset>& FillDelegates = FillCustomAttributeDelegates.FindOrAdd(AssetType::StaticClass());		\
			FillDelegates.Add(UE::Interchange::FFillAttributeToAsset::CreateUObject(this, &NodeClassName::FillCustom##AttributeName##FromAsset));	\
		}																																			\
		return true;																																\
	}																																				\
	return false;

#else //#if WITH_ENGINE

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)															\
	FString OperationName = GetTypeName() + TEXT(".Set" #AttributeName);																				\
	return InterchangePrivateNodeBase::SetCustomAttribute<AttributeType>(*Attributes, Macro_Custom##AttributeName##Key, OperationName, AttributeValue);

#define IMPLEMENT_NODE_ATTRIBUTE_SETTER_WITH_CUSTOM_DELEGATE(NodeClassName, AttributeName, AttributeType, AssetType)								\
	IMPLEMENT_NODE_ATTRIBUTE_SETTER(NodeClassName, AttributeName, AttributeType, AssetType)

#endif //#if WITH_ENGINE

#define INTERCHANGE_BASE_NODE_ADD_ATTRIBUTE(ValueType)																									\
UE::Interchange::FAttributeStorage::TAttributeHandle<ValueType> Handle = RegisterAttribute<ValueType>(UE::Interchange::FAttributeKey(NodeAttributeKey), Value);	\
return Handle.IsValid();

#define INTERCHANGE_BASE_NODE_GET_ATTRIBUTE(ValueType)																						\
if(HasAttribute(UE::Interchange::FAttributeKey(NodeAttributeKey)))																			\
{																																			\
	UE::Interchange::FAttributeStorage::TAttributeHandle<ValueType> Handle = GetAttributeHandle<ValueType>(UE::Interchange::FAttributeKey(NodeAttributeKey));	\
	if (Handle.IsValid())																													\
	{																																		\
		return (Handle.Get(OutValue) == UE::Interchange::EAttributeStorageResult::Operation_Success);										\
	}																																		\
}																																			\
return false;

//Interchange namespace
namespace UE
{
	namespace Interchange
	{
		DECLARE_DELEGATE_RetVal_OneParam(bool, FApplyAttributeToAsset, UObject*);
		DECLARE_DELEGATE_RetVal_OneParam(bool, FFillAttributeToAsset, UObject*);

		/**
		 * Helper struct use to declare static const data we use in the UInterchangeBaseNode
		 * Node that derive from UInterchangeBaseNode can also add a struct that derive from this one to add there static data
		 * @note: The static data are mainly for holding Attribute keys. All attributes that are always available for a node should be in this class or a derived class.
		 */
		struct INTERCHANGECORE_API FBaseNodeStaticData
		{
			static const FAttributeKey& UniqueIDKey();
			static const FAttributeKey& DisplayLabelKey();
			static const FAttributeKey& ParentIDKey();
			static const FAttributeKey& IsEnabledKey();
			static const FString& TargetAssetIDsKey();
			static const FString& GetFactoryDependenciesBaseKey();
			static const FAttributeKey& ClassTypeAttributeKey();
			static const FAttributeKey& AssetNameKey();
			static const FAttributeKey& NodeContainerTypeKey();
			static const FAttributeKey& ReimportStrategyFlagsKey();
		};

	} //ns Interchange
} //ns UE

UENUM(BlueprintType)
enum class EInterchangeNodeContainerType : uint8
{
	None,
	TranslatedScene,
	TranslatedAsset,
	FactoryData
};

UENUM()
enum class EReimportStrategyFlags : uint8
{
	ApplyNoProperties, //Do not apply any property when re-importing, simply change the source data
	ApplyPipelineProperties, //Always apply all pipeline specified properties
	ApplyEditorChangedProperties //Always apply all pipeline properties, but leave the properties modified in editor since the last import
};

/**
 * This struct is used to store and retrieve key value attributes. The attributes are store in a generic FAttributeStorage which serialize the value in a TArray64<uint8>
 * See UE::Interchange::EAttributeTypes to know the supported template types
 * This is an abstract class. This is the base class of the interchange node graph format, all class in this format should derive from this class
 */
UCLASS(BlueprintType, Experimental)
class INTERCHANGECORE_API UInterchangeBaseNode : public UObject
{
	GENERATED_BODY()

public:
	UInterchangeBaseNode();

	/**
	 * Initialize the base data of the node
	 * @param UniqueID - The uniqueId for this node
	 * @param DisplayLabel - The name of the node
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void InitializeNode(const FString& UniqueID, const FString& DisplayLabel, const EInterchangeNodeContainerType NodeContainerType);

	/**
	 * Return the node type name of the class, we use this when reporting error
	 */
	virtual FString GetTypeName() const;

	/**
	 * Icon name, use to retrieve the brush when we display the node in any UI
	 */
	virtual FName GetIconName() const;

	/**
	 * UI that inspect node attribute call this to give a readable name to attribute key
	 */
	virtual FString GetKeyDisplayName(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * UI that inspect node attribute call this to display or hide an attribute
	 */
	virtual bool ShouldHideAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * UI that inspect node attribute call this to display the attribute under the returned category
	 */
	virtual FString GetAttributeCategory(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * Add an attribute to the node
	 * @param NodeAttributeKey - The key of the attribute
	 * @param Value - The attribute value.
	 *
	 */
	template<typename T>
	UE::Interchange::FAttributeStorage::TAttributeHandle<T> RegisterAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey, const T& Value)
	{
		const UE::Interchange::EAttributeStorageResult Result = Attributes->RegisterAttribute(NodeAttributeKey, Value);
		
		if (IsAttributeStorageResultSuccess(Result))
		{
			return Attributes->GetAttributeHandle<T>(NodeAttributeKey);
		}
		LogAttributeStorageErrors(Result, TEXT("RegisterAttribute"), NodeAttributeKey);
		return UE::Interchange::FAttributeStorage::TAttributeHandle<T>();
	}

	/**
	 * Return true if the node contain an attribute with the specified Key
	 * @param nodeAttributeKey - The key of the searched attribute
	 *
	 */
	virtual bool HasAttribute(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * This function return an attribute type for the specified Key. Return type None if the key is invalid
	 *
	 * @param NodeAttributeKey - The key of the attribute
	 */
	virtual UE::Interchange::EAttributeTypes GetAttributeType(const UE::Interchange::FAttributeKey& NodeAttributeKey) const;

	/**
	 * This function return an  attribute handle for the specified Key.
	 * If there is an issue with the KEY or storage the method will trip a check, always make sure you have a valid key before calling this
	 * 
	 * @param NodeAttributeKey - The key of the attribute
	 */
	template<typename T>
	UE::Interchange::FAttributeStorage::TAttributeHandle<T> GetAttributeHandle(const UE::Interchange::FAttributeKey& NodeAttributeKey) const
	{
		return Attributes->GetAttributeHandle<T>(NodeAttributeKey);
	}

	/**
	 * Use this function to query all the node attribute keys
	 */
	void GetAttributeKeys(TArray<UE::Interchange::FAttributeKey>& AttributeKeys) const;

	/**
	 * Remove any attribute from this node. Return false if we cannot remove it. If the attribute do not exist it will return true.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool RemoveAttribute(const FString& NodeAttributeKey);

	/**
	 * Add a boolean attribute to this node. Return false if the attribute do not exist or if we cannot add it
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddBooleanAttribute(const FString& NodeAttributeKey, const bool& Value);
	
	/**
	 * Get a boolean attribute from this node. Return false if the attribute do not exist
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetBooleanAttribute(const FString& NodeAttributeKey, bool& OutValue) const;

	/**
	 * Add a int32 attribute to this node. Return false if the attribute do not exist or if we cannot add it
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddInt32Attribute(const FString& NodeAttributeKey, const int32& Value);
	
	/**
	 * Get a int32 attribute from this node. Return false if the attribute do not exist
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetInt32Attribute(const FString& NodeAttributeKey, int32& OutValue) const;

	/**
	 * Add a float attribute to this node. Return false if the attribute do not exist or if we cannot add it
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddFloatAttribute(const FString& NodeAttributeKey, const float& Value);
	
	/**
	 * Get a float attribute from this node. Return false if the attribute do not exist
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetFloatAttribute(const FString& NodeAttributeKey, float& OutValue) const;

	/**
	 * Add a string attribute to this node. Return false if the attribute do not exist or if we cannot add it
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddStringAttribute(const FString& NodeAttributeKey, const FString& Value);
	
	/**
	 * Get a string attribute from this node. Return false if the attribute do not exist
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetStringAttribute(const FString& NodeAttributeKey, FString& OutValue) const;

	/**
	 * Add a FLinearColor attribute to this node. Return false if the attribute do not exist or if we cannot add it
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddLinearColorAttribute(const FString& NodeAttributeKey, const FLinearColor& Value);
	
	/**
	 * Get a FLinearColor attribute from this node. Return false if the attribute do not exist
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool GetLinearColorAttribute(const FString& NodeAttributeKey, FLinearColor& OutValue) const;

	template<typename AttributeType>
	bool GetAttribute(const FString& NodeAttributeKey, AttributeType& OutValue) const
	{
		INTERCHANGE_BASE_NODE_GET_ATTRIBUTE(AttributeType);
	}
	template<typename AttributeType>
	bool SetAttribute(const FString& NodeAttributeKey, const AttributeType& Value)
	{
		INTERCHANGE_BASE_NODE_ADD_ATTRIBUTE(AttributeType);
	}

	/**
	 * Adds the delegates that will read and write the attribute value to a UObject.
	 * @param NodeAttributeKey	The key of the attribute for which we are adding the delegates.
	 * @param ObjectClass		The class of the object that we will apply and fill on.
	 * @param PropertyName		The name of the property of the ObjectClass that we will read and write to.
	 */
	template<typename AttributeType>
	inline void AddApplyAndFillDelegates(const FString& NodeAttributeKey, UClass* ObjectClass, const FName PropertyName);

	/**
	 * Writes an attribute value to a UObject.
	 * @param NodeAttributeKey	The key for the attribute to write.
	 * @param Object			The object to write to.
	 * @param PropertyName		The name of the property to write to on the Object.
	 * @result					True if we succeeded.
	 */
	template<typename AttributeType>
	inline bool ApplyAttributeToObject(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName) const;

	/**
	 * Reads an attribute value from a UObject.
	 * @param NodeAttributeKey	The key for the attribute to update.
	 * @param Object			The object to read from.
	 * @param PropertyName		The name of the property to read from on the Object.
	 * @result					True if we succeeded.
	 */
	template<typename AttributeType>
	inline bool FillAttributeFromObject(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName);

	/**
	 * Return the unique id pass in the constructor.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetUniqueID() const;

	/**
	 * Return the display label.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetDisplayLabel() const;

	/**
	 * Change the display label.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetDisplayLabel(const FString& DisplayName);

	/**
	 * Return the reimport strategy flags.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	EReimportStrategyFlags GetReimportStrategyFlags() const;

	/**
	 * Change the reimport strategy flags.
	 *
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetReimportStrategyFlags(const EReimportStrategyFlags& ReimportStrategyFlags);

	/**
	 * Return the parent unique id. In case the attribute does not exist it will return InvalidNodeUid()
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	FString GetParentUid() const;

	/**
	 * Set the parent unique id.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetParentUid(const FString& ParentUid);

	/**
	 * This function allow to retrieve the number of factory dependencies for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	int32 GetFactoryDependenciesCount() const;

	/**
	 * This function allow to retrieve the dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetFactoryDependencies(TArray<FString>& OutDependencies ) const;

	/**
	 * This function allow to retrieve one dependency for this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetFactoryDependency(const int32 Index, FString& OutDependency) const;

	/**
	 * Add one dependency to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddFactoryDependencyUid(const FString& DependencyUid);

	/**
	 * Remove one dependency from this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool RemoveFactoryDependencyUid(const FString& DependencyUid);

	/**
	 * Get number of target assets relating to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	int32 GetTargetNodeCount() const;

	/**
	 * Get target assets relating to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	void GetTargetNodeUids(TArray<FString>& OutTargetAssets) const;

	/**
	 * Add asset node UID relating to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool AddTargetNodeUid(const FString& AssetUid) const;

	/**
	 * Remove asset node UID relating to this object.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool RemoveTargetNodeUid(const FString& AssetUid) const;

	/**
	 * IsEnable true mean that the node will be import/export, if false it will be discarded.
	 * Return false if this node was disabled. Return true if the attribute is not there or if it was enabled.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool IsEnabled() const;

	/**
	 * Set the IsEnable attribute to determine if this node should be part of the import/export process
	 * @param bIsEnabled - The enabled state we want to set this node. True will import/export the node, fals will not.
	 * @return true if it was able to set the attribute, false otherwise.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	bool SetEnabled(const bool bIsEnabled);

	/**
	 * Return the node container type which define the purpose of the node (Factory node, translated scene node or translated asset node).
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	EInterchangeNodeContainerType GetNodeContainerType() const;

	/**
	 * Return a FGuid build from the FSHA1 of all the attribute data contain in the node.
	 *
	 * @note the attribute are sorted by key when building the FSHA1 data. The hash will be deterministic for the same data whatever
	 * the order we add the attributes.
	 * This function interface is pure virtual
	 */
	virtual FGuid GetHash() const;

	/**
	 * Optional, Any node that can import/export an object should return the UClass of the object so we can find factory/writer
	 */
	virtual class UClass* GetObjectClass() const;

	/**
	 * Optional, Any node that can import/export an asset should set the proper name we will give to the asset.
	 * If the attribute was never set, it will return GetDisplayLabel.
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	virtual FString GetAssetName() const;

	/**
	 * Set the name we want for the imported asset this node represent. The asset factory will call GetAssetName()
	 */
	UFUNCTION(BlueprintCallable, Category = "Interchange | Node")
	virtual bool SetAssetName(const FString& AssetName);

	/** Return the invalid unique ID */
	static FString InvalidNodeUid();

	/**
	 * Each Attribute that was set and have a delegate set for the specified UObject->UClass will
	 * get the delegate execute so it apply the attribute to the UObject property.
	 * See the macros IMPLEMENT_NODE_ATTRIBUTE_SETTER at the top of the file to know how delegates are setup for property.
	 */
	void ApplyAllCustomAttributeToObject(UObject* Object) const;

	void FillAllCustomAttributeFromObject(UObject* Object) const;

	/**
	 * Serialize the node, by default only the attribute storage is serialize for a node
	 */
	virtual void Serialize(FArchive& Ar) override;

	static void CompareNodeStorage(UInterchangeBaseNode* NodeA, const UInterchangeBaseNode* NodeB, TArray<UE::Interchange::FAttributeKey>& RemovedAttributes, TArray<UE::Interchange::FAttributeKey>& AddedAttributes, TArray<UE::Interchange::FAttributeKey>& ModifiedAttributes);
	
	static void CopyStorageAttributes(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode, TArray<UE::Interchange::FAttributeKey>& AttributeKeys);

	static void CopyStorage(const UInterchangeBaseNode* SourceNode, UInterchangeBaseNode* DestinationNode);
	
	virtual void AppendAssetRegistryTags(TArray<FAssetRegistryTag>& OutTags) const
	{
		return;
	}

	UPROPERTY()
	mutable FSoftObjectPath ReferenceObject;
protected:
	/** The storage use to store the Key value attribute for this node. */
	TSharedPtr<UE::Interchange::FAttributeStorage, ESPMode::ThreadSafe> Attributes;

	/* This array hold the delegate to apply the attribute that has to be set on an UObject */
	TMap<UClass*, TArray<UE::Interchange::FApplyAttributeToAsset>> ApplyCustomAttributeDelegates;

	TMap<UClass*, TArray<UE::Interchange::FFillAttributeToAsset>> FillCustomAttributeDelegates;

	bool bIsInitialized = false;

	/**
	 * Those dependencies are use by the interchange parsing task to make sure the asset are created in the correct order.
	 * Example: Mesh factory node will have dependencies on material factory node
	 *          Material factory node will have dependencies on texture factory node
	 */
	UE::Interchange::TArrayAttributeHelper<FString> FactoryDependencies;

	/**
	 * This tracks the IDs of asset nodes which are the target of factories
	 */
	mutable UE::Interchange::TArrayAttributeHelper<FString> TargetNodes;
};

template<typename AttributeType>
inline void UInterchangeBaseNode::AddApplyAndFillDelegates(const FString& NodeAttributeKey, UClass* ObjectClass, const FName PropertyName)
{
	TArray<UE::Interchange::FApplyAttributeToAsset>& ApplyDelegates = ApplyCustomAttributeDelegates.FindOrAdd(ObjectClass);
	ApplyDelegates.Add(
		UE::Interchange::FApplyAttributeToAsset::CreateLambda(
		[NodeAttributeKey, PropertyName, this](UObject* Asset)
		{
			return ApplyAttributeToObject<AttributeType>(NodeAttributeKey, Asset, PropertyName);
		})
	);

	TArray<UE::Interchange::FFillAttributeToAsset>& FillDelegates = FillCustomAttributeDelegates.FindOrAdd(ObjectClass);
	FillDelegates.Add(
		UE::Interchange::FApplyAttributeToAsset::CreateLambda(
		[NodeAttributeKey, PropertyName, this](UObject* Asset)
		{
			return FillAttributeFromObject<AttributeType>(NodeAttributeKey, Asset, PropertyName);
		})
	);
}

template<typename AttributeType>
inline bool UInterchangeBaseNode::ApplyAttributeToObject(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName) const
{
	if (!Object)
	{
		return false;
	}

	AttributeType ValueData;
	if (GetAttribute<AttributeType>(NodeAttributeKey, ValueData))
	{
		TVariant<UObject*, uint8*> Container;
		Container.Set<UObject*>(Object);
		if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
		{
			AttributeType* PropertyValueAddress;
			if (Container.IsType<UObject*>())
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<AttributeType>(Container.Get<UObject*>());
			}
			else
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<AttributeType>(Container.Get<uint8*>());
			}

			*PropertyValueAddress = ValueData;
		}
		return true;
	}
	return false;
}

/**
 * Specialized version of ApplyAttributeToObject for strings.
 * If the target property is a FObjectPropertyBase, treat the string as an object path.
 */
template<>
inline bool UInterchangeBaseNode::ApplyAttributeToObject<FString>(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName) const
{
	if (!Object)
	{
		return false;
	}

	FString ValueData;
	if (GetAttribute<FString>(NodeAttributeKey, ValueData))
	{
		TVariant<UObject*, uint8*> Container;
		Container.Set<UObject*>(Object);
		if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
		{
			FString* PropertyValueAddress;
			if (Container.IsType<UObject*>())
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<FString>(Container.Get<UObject*>());
			}
			else
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<FString>(Container.Get<uint8*>());
			}

			if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				ObjectProperty->SetObjectPropertyValue(PropertyValueAddress, FSoftObjectPath(ValueData).TryLoad());
			}
			else
			{
				*PropertyValueAddress = ValueData;
			}
		}
		return true;
	}
	return false;
}

/**
 * Specialized version of ApplyAttributeToObject for bools.
 * If the target property is a FBoolProperty, treat the propertyg as a bitfield.
 */
template<>
inline bool UInterchangeBaseNode::ApplyAttributeToObject<bool>(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName) const
{
	if (!Object)
	{
		return false;
	}

	bool bValueData;
	if (GetAttribute<bool>(NodeAttributeKey, bValueData))
	{
		TVariant<UObject*, uint8*> Container;
		Container.Set<UObject*>(Object);
		if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
		{
			bool* PropertyValueAddress;
			if (Container.IsType<UObject*>())
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<bool>(Container.Get<UObject*>());
			}
			else
			{
				PropertyValueAddress = Property->ContainerPtrToValuePtr<bool>(Container.Get<uint8*>());
			}

			// Support for bitfields
			if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
			{
				BoolProperty->SetPropertyValue(PropertyValueAddress, bValueData);
			}
			else
			{
				*PropertyValueAddress = bValueData;
			}
		}
		return true;
	}
	return false;
}

template<typename AttributeType>
inline bool UInterchangeBaseNode::FillAttributeFromObject(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName)
{
	TVariant<UObject*, uint8*> Container;
	Container.Set<UObject*>(Object);
	if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
	{
		AttributeType* PropertyValueAddress;
		if (Container.IsType<UObject*>())
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<AttributeType>(Container.Get<UObject*>());
		}
		else
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<AttributeType>(Container.Get<uint8*>());
		}

		// Support for bitfields
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			const bool bPropertyValue = BoolProperty->GetPropertyValue(PropertyValueAddress);
			return SetAttribute(NodeAttributeKey, bPropertyValue);
		}
		else
		{
			return SetAttribute(NodeAttributeKey, *PropertyValueAddress);
		}
	}

	return false;
}

/**
 * Specialized version of FillAttributeFromObject for strings.
 * If the target property is a FObjectPropertyBase, treat the string as an object path.
 */
template<>
inline bool UInterchangeBaseNode::FillAttributeFromObject<FString>(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName)
{
	TVariant<UObject*, uint8*> Container;
	Container.Set<UObject*>(Object);
	if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
	{
		FString* PropertyValueAddress;
		if (Container.IsType<UObject*>())
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<FString>(Container.Get<UObject*>());
		}
		else
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<FString>(Container.Get<uint8*>());
		}

		if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			UObject* ObjectValue = ObjectProperty->GetObjectPropertyValue(PropertyValueAddress);
			return SetAttribute(NodeAttributeKey, ObjectValue->GetPathName());
		}
		else
		{
			return SetAttribute(NodeAttributeKey, *PropertyValueAddress);
		}
	}

	return false;
}

/**
 * Specialized version of FillAttributeFromObject for bools.
 * If the target property is a FBoolProperty, treat the property as a bitfield.
 */
template<>
inline bool UInterchangeBaseNode::FillAttributeFromObject<bool>(const FString& NodeAttributeKey, UObject* Object, const FName PropertyName)
{
	TVariant<UObject*, uint8*> Container;
	Container.Set<UObject*>(Object);
	if(FProperty* Property = InterchangePrivateNodeBase::FindPropertyByPathChecked(Container, Object->GetClass(), PropertyName.ToString()))
	{
		bool* PropertyValueAddress;
		if (Container.IsType<UObject*>())
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<bool>(Container.Get<UObject*>());
		}
		else
		{
			PropertyValueAddress = Property->ContainerPtrToValuePtr<bool>(Container.Get<uint8*>());
		}

		// Support for bitfields
		if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
		{
			const bool bPropertyValue = BoolProperty->GetPropertyValue(PropertyValueAddress);
			return SetAttribute(NodeAttributeKey, bPropertyValue);
		}
		else
		{
			return SetAttribute(NodeAttributeKey, *PropertyValueAddress);
		}
	}

	return false;
}
