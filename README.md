
# Open Unreal Utilities - JSON Data Assets

This plugin adds a new data asset type to Unreal Engine that allows asset serialization to JSON text files.

The plugin allows both creating assets in editor, as well as dynamic loading of asset data at runtime, making it a potential 
foundation for user generated content and game patching tools that circumvent Unreal's usual cook worfklow.

When loaded into memory, each JSON text file gets a binary package generated in a new `/JsonData/` package root. In the editor,
these files are also stored on disk in the `Saved/JsonDataCache/` directory, which enables asset registry features
including reference search and size map visualization.

## Limitations

One of the fundamental shortcomings of this plugin is that json assets **cannot be loaded asynchronously or implicitly!**

This can be mostly blamed on the complexity and black-box nature of the existing async asset loading code in the 
engine. Implementing a single asset load asynchronously is relatively straight forward, but any meaningful loading 
code will want to resolve asset dependencies asynchronously as well, allowing mixing both JSON assets and regular
binary assets.

As a result you will want to keep your JSON data light and will likely also want to employ some kind of asset 
pre-loading policy to prevent hitches while resolving asset references.

## Usage

### Data Directories

JSON files are placed in `Data/` directories next to the `Content/` directories for regular binary uassets.
The game project data folder is enabled by default, but plugins have to be registered at module startup to allow storing content.

```c++
FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddLambda([]() {
    UJsonDataAssetSubsystem::Get().AddPluginDataRoot(TEXT("PluginName"));
});
```

The name of these data folders can be adjusted by changing the console variable `ouu.JsonData.SourceUncooked`, but this is a global setting
that will break most `OUU.JsonData.*` tests, unless you rename the [Data/](Data/) folder of this plugin.

See [`JsonDataAssetConsoleVariables.cpp`](Source/OUUJsonDataRuntime/Private/JsonDataAssetConsoleVariables.cpp) for more info

### Loading and Garbage Collection

On the lowest level, json assets can be referenced via `FJsonDataAssetPath`s. This is what the subsystem works with internally, but any loaded objects must be additionally referenced by transient object properties or other implementations preventing garbace collection.

```c++
auto Path = FJsonDataAssetPath::FromPackagePath("/JsonData/Some/Folder/BarAsset");
UJsonDataAsset* DataAsset = Path.LoadSynchronous();
```

As you can see, the paths notably lack the following convenience features that you will probably want to have for editable properties and
asset references in game code:

- Paths do not prevent garbage collection
- Paths are not statically typed

To solve this, we offer two types of smart pointers: `TSoftJsonDataAssetPtr<T>` returns correctly downcasted pointers 
to data asset objects, while retaining the same loading and GC behavior.
`TJsonDataAssetPtr<T>` expands on this and adds internal hard references to the generated objects to prevent garbage collection when used in UPROPERTIES.
Even these paths **do not load assets implicitly** though, but only at the time of accessing the pointer!

```c++
auto BarPath = FJsonDataAssetPath::FromPackagePath("/JsonData/Some/Folder/Bar");

// This will load the asset and retain it in-memory as long as BarAssetPtr is reachable by the garbage collector
// (either in a UPROPERTY or manual FGCObject implementation).
TSoftJsonDataAssetPtr<UBar> BarAssetPtr{BarPath};

// Get the actual object pointer.
UBar* BarAsset = BarAssetPtr.Get();
```

Unfortunately, the parsing requirements of Unreal Header Tool prevent using these templated types directly unless you
make some modifications to the UHT source code. They are still great for native C++ code and templates. Any code that
interfaces with UHT reflection or the Blueprint VM will either need to use the untemplates base class 
`FJsonDataAssetPtr` or write some boilerplate code to inherit from the pointer types:

```c++
USTRUCT(BlueprintType, Meta = (JsonDataAssetClass = "/Script/Foo.Bar"))
struct FBarSoftPtr
#if CPP
	: public TSoftJsonDataAssetPtr<const UBar>
#else
	: public FSoftJsonDataAssetPtr
#endif
{
	GENERATED_BODY()

public:
	using TSoftJsonDataAssetPtr::TSoftJsonDataAssetPtr;
};

OUU_DECLARE_JSON_DATA_ASSET_PTR_TRAITS(FBarSoftPtr);
```

## Cooking JSON Assets

At this time, all json files and their dependencies are included in every cook. This is implemented in `UJsonDataAssetSubsystem::ModifyCook`.

The json files themselves are preserved as plain-text files during cook and bundled with the game. They are intended to be
changeable after packaging, so they are not suited for any kind of sensitive data that should not be directly 
accessible to players.

## User Generated Content

So far there are no systems that simplify dynamic asset discovery. In editor, we rely on the asset registry and content browser to 
discover files on disk and all of our current uses at runtime rely on assets being explicitly referenced.

However, adding dynamic content discovery for games should be relatively straight forward to implement.

Keep in mind, that there are some optimizations that assume content to be identical between participants in a 
networked environment (see [Netwoking](#networking) below).

## Networking

UJsonDataAsset isn't directly supported for networking, because even though they could be stably named for net connections, loading them 
via the regular asset loader is not supported, which means `UPackageMap` won't be able to resolve `FNetworkGUID`s without further modifications.

I'm still considering adding such features, but this will certainly wait until Epic's rollout of
[Iris](https://docs.unrealengine.com/5.2/en-US/introduction-to-iris-in-unreal-engine/) is more advanced.

For the time being, assets have to be net-referenced via `FJsonDataAssetPath`, or any of the smart pointers discussed
[above](#loading-and-garbage-collection).

### Fast Net Serialization

Similar to fast net serialization of `FGameplayTag`s, all JSON asset smart pointers and paths support an optimized network serialization 
implementation that relies on indexing json files at game startup. The feature is enabled by default, but some games that can't
guarantee that JSON files will be identical on all network participants, may opt out of this system by setting the console variable
`ouu.JsonData.UseFastNetSerialization = false`.

## Content Browser Data Source

The content browser normally displays only the JSON source files via custom content browser data source and hides all the generated objects.
This is mostly to avoid confusion when checking an assets source control state, because only the JSON source files should ever be checked in - 
the uasset cache will be automatically updated at every engine startup to reflect the current state of source files.
This is implemented in `UJsonDataAssetSubsystem::CleanupAssetCache`.

To make this work correctly, VCS tools should be configured to update file timestamps.
For Perforce this means syncing files with `modtime` disabled, which is the default behavior.

Other than this, the source file and the generated asset will mostly have the same state and generally support the same content browser actions.

If you want to see both the source and generated files, you can activate the `Show Generated Json Data Assets` filter in `Misc` category
(next to option to show Redirector files).

## Serialization Implementation

This section covers the serialization of properties, which is for the most part a modified version of Epic's `FJsonObjectConverter` with support for [asset versioning](#asset-versioning), [delta serialization](#delta-serialization) and [custom asset versions](#custom-asset-versions). Currently, all properties that do NOT have a 'Transient' flag will be exported. Imports do not check for property flags.

### Delta Serialization

Outside of cooking, JSON data is generally serialized with a delta serialization approach similar to the binary property serialization.
Only modified properties are saved into the text files, allowing propagation of inherited class defaults.

During [cook](#cooking-json-assets), a full export of all object properties is made to show a full overview of available properties to end users.

### Asset Versioning

All json assets get a header that contains the assets class path and some additional meta information:

```json
{
    "Class": "/Script/OUUJsonDataTests.TestJsonDataAsset",
	"EngineVersion": "5.2.1-31885+++MINERVA+minerva-code",
	"IsLicenseeVersion": true,
	"CustomVersions":
	{
        // List of custom version GUIDs
	},
	"Data":
	{
        // The bulk of property data goes here
    }
}
```

The `EngineVersion` and `IsLicenseeVersion` fields are used to generate an `FEngineVersion` to perform a compatibility check similar to Epic's implementation of [versioning of assets and packages](https://docs.unrealengine.com/5.2/en-US/versioning-of-assets-and-packages-in-unreal-engine/).

### Custom Asset Versions

JSON assets have a primitive support for data post-processing after import via custom asset versioning. For this you will have to override 
`UJsonDataAsset::GetRelevantCustomVersions` and `UJsonDataAsset::PostLoadJsonData`, e.g.:

```c++
bool UBar::PostLoadJsonData(const FEngineVersion& EngineVersion,
                            const FJsonDataCustomVersions& CustomVersions,
                            TSharedRef<FJsonObject> JsonObject)
{
	if (CustomVersions.GetCustomVersion(FBarVersion::GUID) < FBarVersion::SomeChange)
	{
        const TArray<TSharedPtr<FString>>* OldArray = nullptr;
        if (JsonObject->TryGetArrayField(TEXT("OldArrayMember"), OUT OldArray))
        {
            // do something with OldArray...
        }
	}

	return true;
}

TSet<FGuid> UBar::GetRelevantCustomVersions() const
{
	return {FBarVersion::GUID};
}
```

This mechanism only allows implementing post-load code on the topmost level, so nested struct properties must keep fully backward compatible 
text import functions to avoid breaking old json data.

## Coding Conventions

The plugin adheres to the [Open Unreal Conventions](https://jonasreich.github.io/OpenUnrealConventions/) which are extended
coding conventions based on Epic Games' coding guidelines. 

## Licensing

The *OUU JSON Data Assets* plugin plugin is a sister-plugin of the [Open Unreal Utilities](https://github.com/JonasReich/OpenUnrealUtilities) and is licensed under the MIT license.

See [LICENSE.md](LICENSE.md)

## Contributing

This plugin was initially developed by Jonas Reich with help of colleagues at Grimlore Games (see [CREDITS.md](CREDITS.md)).

Anyone is invited to create pull-requests to the github source for any additions or modifications you make to the plugin:
https://github.com/JonasReich/OUUJsonDataAsset
