# Blueprint-Library SQLite3 for Unreal Engine

## About this fork 

Based on [unreal-sqlite3 by Squareys](https://github.com/Squareys/unreal-sqlite3) but using SQLiteCore from UnrealEngine (should work on 4.22+ for UE4 you have to change the engine version in the uplugin file)

I added some support for sqlite3 to have database modifications reachable from Blueprints. Example:

https://blueprintue.com/blueprint/z9xg6l0u/ https://blueprintue.com/blueprint/sw7cysyz/

If you have high-performance requirements, you need to enhance it for using prepared statements in the desired way. This has not been implemented, yet. 

# Works On

Compiled and basic functionality:

OS | Tested
---|-------
Windows x64 | ✓
Linux x64 | ✓
Android | ✓


UE4 Version | Tested
---|-------
4.27 | ✓
5.0.x | ✓

# Installation

Copy this plugin (like Download as ZIP) into the folder **Plugins/CISQLite3** on your project and a start of the project should compile the plugin automatically, if the project is C++. If you don't have a C++ project, then you can just make one for the sole purpose of compiling this plugin. Alternative you can install it as Engine Plugin in the Engine Plugins directory (like **Epic Games/5.0/Engine/Plugins/Runtime/CISQLite3**).

# Usage

(More usages to come....)

## C++

And here's a simple sample in C++:

Header:
```c++
UCLASS()
class SQLITE_API AMyActor : public AActor
{

  GENERATED_BODY()

public:

  UFUNCTION(BlueprintCallable, Category = "My Actor")
  bool GetMyStats();

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "My Actor")
  FString Name;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "My Actor")
  int32 Age;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "My Actor")
  FString Gender;

  UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "My Actor")
  float Height;

};

```

CPP:

```c++
#include "MyActor.h"
#include "SQLiteDatabase.h"

bool AMyActor::GetMyStats()
{

  FString dbName = TEXT("TestDatabase");
  FString actorName = TEXT("Bruce Willis");

  if (!USQLiteDatabase::IsDatabaseRegistered(dbName))
  {
    USQLiteDatabase::RegisterDatabase(dbName, "Databases/TestDatabase.db", true);
  }

  bool didPopulate = USQLiteDatabase::GetDataIntoObject(dbName, FString::Printf(TEXT("SELECT Name, Age, Gender, Height FROM Actors WHERE Name = \"%s\""), *actorName), this);

  return didPopulate;

}
```

# License & Copyright

## CISQLite3

The MIT License (MIT)

Copyright (c) 2015 Jussi Saarivirta & KhArtNJava (SQLite3UE4)

Copyright (c) 2016 conflict.industries

Copyright (c) 2019 Jonathan Hale (Vhite Rabbit GbR)
Copyright (c) 2019 Rehub GmbH

## SQLite3

The author disclaims copyright to this source code. In place of a legal notice, here is a blessing:

May you do good and not evil.

May you find forgiveness for yourself and forgive others.

May you share freely, never taking more than you give.
