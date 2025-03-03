#include "SQLiteDatabase.h"
#include "CISQLite3PrivatePCH.h"

#define LOGSQLITE(verbosity, text) UE_LOG(LogDatabase, verbosity, TEXT("SQLite: %s"), text)

TMap<FString, FString> USQLiteDatabase::Databases;
TMap<FString, sqlite3*> USQLiteDatabase::SQLite3Databases;

//--------------------------------------------------------------------------------------------------------------

USQLiteDatabase::USQLiteDatabase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::CreateDatabase(const FString& Filename, bool RelativeToProjectContentDirectory)
{
	const FString actualFilename = RelativeToProjectContentDirectory ? FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()) + Filename : Filename;

    sqlite3* db;
    int res = sqlite3_open(TCHAR_TO_ANSI(*actualFilename), &db);
    if (res == SQLITE_OK)
    {
        sqlite3_close(db);
        return true;
    }
    else {
		LOGSQLITE(Error, *FString::Printf(TEXT("Could not create database, error code: %d"), res));
    }

	return false;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::RegisterDatabase(const FString& Name, const FString& Filename, bool RelativeToProjectContentDirectory, bool KeepOpen)
{
	const FString actualFilename = RelativeToProjectContentDirectory ? FPaths::ProjectContentDir() + Filename : Filename;

	if (!IsValidDatabase(actualFilename, true))
	{
		FString message = "Unable to add database '" + actualFilename + "', it is not valid (problems opening it)!";
		LOGSQLITE(Error, *message);
		return false;
	}

	if (!IsDatabaseRegistered(Name))
	{
        Databases.Add(Name, actualFilename);
        FString successMessage = "Registered SQLite database '" + actualFilename + "' successfully.";
        LOGSQLITE(Verbose, *successMessage);
    }
    else {
		FString message = "Database '" + actualFilename + "' is already registered, skipping.";
		LOGSQLITE(Warning, *message);
    }

    if (KeepOpen) {
        sqlite3* db;
        if (sqlite3_open(TCHAR_TO_ANSI(*actualFilename), &db) == SQLITE_OK)
        {
            SQLite3Databases.Add(Name, db);
        }
    }

	return true;

}

//--------------------------------------------------------------------------------------------------------------

void USQLiteDatabase::UnregisterDatabase(const FString& Name) {
    /* Remove in case KeepOpen flag was set to true */
    if (SQLite3Databases.Contains(Name)) {
        sqlite3_close(SQLite3Databases[Name]);
        SQLite3Databases.Remove(Name);
    }
    Databases.Remove(Name);
}

//--------------------------------------------------------------------------------------------------------------
TArray<uint8> USQLiteDatabase::Dump(const FString& DatabaseName) {
    sqlite3* Db;
    const bool keepOpen = SQLite3Databases.Contains(DatabaseName);
    if (keepOpen) {
        Db = SQLite3Databases[DatabaseName];
    } else {
        const FString* databaseName = Databases.Find(DatabaseName);
        if (!databaseName) {
            LOGSQLITE(Error, TEXT("DB not registered."));
            return {};
        }

        const int err = sqlite3_open(TCHAR_TO_ANSI(**databaseName), &Db);
        if (err != SQLITE_OK) {
            const char* msg = sqlite3_errmsg(Db);
            if(msg) {
                UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: '%s' (%i), message: '%s'"), sqlite3_errstr(err), err, sqlite3_errmsg(Db));
            } else {
                UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: %s"), sqlite3_errstr(err));
            }
            return {};
        }
    }

    int64 size;
    uint8* ptr = sqlite3_serialize(Db, "main", &size, 0);
    const int err = sqlite3_errcode(Db);
    if (err != SQLITE_OK) {
        const char* msg = sqlite3_errmsg(Db);
        if(msg) {
            UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: '%s' (%i), message: '%s'"), sqlite3_errstr(err), err, sqlite3_errmsg(Db));
        } else {
            UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: %s"), sqlite3_errstr(err));
        }
        return {};
    }
    return TArray<uint8>(ptr, size);
}

bool USQLiteDatabase::Restore(const FString& DatabaseName, const TArray<uint8>& data) {
    sqlite3* Db;
    const bool keepOpen = SQLite3Databases.Contains(DatabaseName);
    if (keepOpen) {
        Db = SQLite3Databases[DatabaseName];
    } else {
        const FString* databaseName = Databases.Find(DatabaseName);
        if (!databaseName) {
            LOGSQLITE(Error, TEXT("DB not registered."));
            return false;
        }

        sqlite3_open(TCHAR_TO_ANSI(**databaseName), &Db);
    }

    const int err = sqlite3_deserialize(Db, "main", 
        const_cast<unsigned char*>(data.GetData()), data.Num(), data.Num(), SQLITE_DESERIALIZE_READONLY);
    if (err != SQLITE_OK) {
        const char* msg = sqlite3_errmsg(Db);
        if(msg) {
            UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: '%s' (%i), message: '%s'"), sqlite3_errstr(err), err, sqlite3_errmsg(Db));
        } else {
            UE_LOG(LogDatabase, Error, TEXT("Error ocurred during serialization, code: %s"), sqlite3_errstr(err));
        }
        return false;
    }
    return true;
}
//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::GetDataIntoObject(const FString& DatabaseName, const FString& Query, UObject* ObjectToPopulate)
{
	//////////////////////////////////////////////////////////////////////////
	// Check input validness.
	//////////////////////////////////////////////////////////////////////////

	if (ObjectToPopulate == NULL)
	{
		LOGSQLITE(Error, TEXT("ObjectToPopulate needs to be set to get any results!"));
		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Validate the database
	//////////////////////////////////////////////////////////////////////////

	if (!IsDatabaseRegistered(DatabaseName) ||
		!IsValidDatabase(Databases[DatabaseName], false))
	{
		LOGSQLITE(Error, *FString::Printf(TEXT("Unable to get data into object, invalid database '%s'"), *DatabaseName));
		return false;
	}

	if (!CanOpenDatabase(Databases[DatabaseName]))
	{
		LOGSQLITE(Error, *FString::Printf(TEXT("Unable to open database '%s'"), *DatabaseName));
		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Get the results
	//////////////////////////////////////////////////////////////////////////

	TUniquePtr<SQLiteQueryResult> queryResult = RunQueryAndGetResults(DatabaseName, Query);

	if (queryResult->Success && queryResult->Results.Num() > 0)
	{
		AssignResultsToObjectProperties(queryResult->Results[0], ObjectToPopulate);
		return true;
	}
	else if (!queryResult->Success)
	{
		LOGSQLITE(Error, *FString::Printf(TEXT("Query resulted in an error: '%s'"), *queryResult->ErrorMessage));
		return false;
	}
	else if (queryResult->Results.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("Query returned zero rows, no data to assign to object properties."));
		return false;
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::GetDataIntoObjectBP(const FSQLiteDatabaseReference& DataSource, TArray<FString> Fields,
	FSQLiteQueryFinalizedQuery Query, UObject* ObjectToPopulate)
{
	//////////////////////////////////////////////////////////////////////////
	// Check input validness.
	//////////////////////////////////////////////////////////////////////////

	if (ObjectToPopulate == NULL)
	{
		LOGSQLITE(Error, TEXT("ObjectToPopulate needs to be set to get any results!"));
		return false;
	}

	if (DataSource.Tables.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("The query needs the table name!"));
		return false;
	}

	if (Fields.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("The query needs fields! You may use * to get all fields."));
		return false;
	}

	//////////////////////////////////////////////////////////////////////////
	// Validate the database
	//////////////////////////////////////////////////////////////////////////

	if (!IsDatabaseRegistered(DataSource.DatabaseName) ||
		!IsValidDatabase(Databases[DataSource.DatabaseName], true))
	{
		LOGSQLITE(Error, TEXT("Unable to get data to object, database validation failed!"));
		return false;
	}


	//////////////////////////////////////////////////////////////////////////
	// Get the results
	//////////////////////////////////////////////////////////////////////////

	FString constructedQuery = ConstructQuery(DataSource.Tables, Fields, Query, 1, 0);

	TUniquePtr<SQLiteQueryResult> queryResult = RunQueryAndGetResults(DataSource.DatabaseName, constructedQuery);

	if (queryResult->Success && queryResult->Results.Num() > 0)
	{
		AssignResultsToObjectProperties(queryResult->Results[0], ObjectToPopulate);
		return true;
	}
	else if (!queryResult->Success)
	{
		LOGSQLITE(Error, *FString::Printf(TEXT("Query resulted in an error: '%s'"), *queryResult->ErrorMessage));
		return false;
	}
	else if (queryResult->Results.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("Query returned zero rows, no data to assign to object properties."));
		return false;
	}

	return false;

}

//--------------------------------------------------------------------------------------------------------------

TMap<FString, FProperty*> USQLiteDatabase::CollectProperties(UObject* SourceObject)
{

	UClass* SourceObjectClass = SourceObject->GetClass();
	TMap<FString, FProperty*> Props;
	for (TFieldIterator<FProperty> PropIt(SourceObjectClass, EFieldIteratorFlags::SuperClassFlags::IncludeSuper);
		PropIt; ++PropIt)
	{
		Props.Add(*PropIt->GetNameCPP(), *PropIt);
	}

	return Props;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::IsDatabaseRegistered(const FString& DatabaseName)
{
	return Databases.Contains(DatabaseName);
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::CanOpenDatabase(const FString& DatabaseFilename)
{
	sqlite3* db;
	if (sqlite3_open(TCHAR_TO_ANSI(*DatabaseFilename), &db) == SQLITE_OK)
	{
		sqlite3_close(db);
		return true;
	}
	return false;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::IsValidDatabase(const FString& DatabaseFilename, bool TestByOpening)
{
	if (FPlatformFileManager::Get().GetPlatformFile().FileExists(*DatabaseFilename))
	{
		if (TestByOpening)
		{
			return CanOpenDatabase(DatabaseFilename);
		}
		else
		{
			return true;
		}
	}

	return false;
}

//--------------------------------------------------------------------------------------------------------------

FSQLiteQueryResult USQLiteDatabase::GetData(const FString& DatabaseName, const FString& Query)
{
	FSQLiteQueryResult result;

	//////////////////////////////////////////////////////////////////////////
	// Validate the database
	//////////////////////////////////////////////////////////////////////////

	if (!IsDatabaseRegistered(DatabaseName) ||
		!IsValidDatabase(Databases[DatabaseName], true))
	{
		LOGSQLITE(Error, TEXT("Unable to get data to object, database validation failed!"));
		result.Success = false;
		result.ErrorMessage = TEXT("Database validation failed");
		return result;
	}

	//////////////////////////////////////////////////////////////////////////
	// Get the results
	//////////////////////////////////////////////////////////////////////////

	TUniquePtr<SQLiteQueryResult> queryResult = RunQueryAndGetResults(DatabaseName, Query);
	result.Success = queryResult->Success;
	result.ErrorMessage = queryResult->ErrorMessage;

	for (auto row : queryResult->Results)
	{
		FSQLiteQueryResultRow outRow;
		for (auto field : row.Fields)
		{
			FSQLiteKeyValuePair outField;
			outField.Key = field.Name;
			outField.Value = field.ToString();

			outRow.Fields.Add(outField);
		}
		result.ResultRows.Add(outRow);
	}

	return result;

}

//--------------------------------------------------------------------------------------------------------------

FSQLiteQueryResult USQLiteDatabase::GetDataBP(const FSQLiteDatabaseReference& DataSource,
	TArray<FString> Fields, FSQLiteQueryFinalizedQuery Query, int32 MaxResults, int32 ResultOffset)
{

	FSQLiteQueryResult result;

	//////////////////////////////////////////////////////////////////////////
	// Check input validness.
	//////////////////////////////////////////////////////////////////////////

	if (DataSource.Tables.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("The query needs at least one table name!"));
		result.Success = false;
		result.ErrorMessage = TEXT("No table given");
		return result;
	}

	if (Fields.Num() == 0)
	{
		LOGSQLITE(Error, TEXT("The query needs fields! You can use * to get all fields."));
		result.Success = false;
		result.ErrorMessage = TEXT("No fields given");
		return result;
	}

	FString constructedQuery = ConstructQuery(DataSource.Tables, Fields, Query, MaxResults, ResultOffset);

	return GetData(DataSource.DatabaseName, constructedQuery);

}

//--------------------------------------------------------------------------------------------------------------

FString USQLiteDatabase::ConstructQuery(TArray<FString> Tables, TArray<FString> Fields,
	FSQLiteQueryFinalizedQuery QueryObject, int32 MaxResults, int32 ResultOffset)
{
	FString fieldString;
	for (int32 i = 0; i < Fields.Num(); i++)
	{
		fieldString.Append(Fields[i] + (i < Fields.Num() - 1 ? "," : ""));
	}

	FString tableList = FString::Join(Tables, TEXT(","));
	TArray<FString> allQueryParams;

	allQueryParams.Add(FString::Printf(TEXT("SELECT %s FROM %s"), *fieldString, *tableList));

	if (QueryObject.Query.Len() > 0)
	{
		allQueryParams.Add(FString::Printf(TEXT("WHERE %s"), *QueryObject.Query));
	}

	if (MaxResults >= 0)
	{
		allQueryParams.Add(FString::Printf(TEXT("LIMIT %i"), MaxResults));
	}

	if (ResultOffset > 0)
	{
		allQueryParams.Add(FString::Printf(TEXT("OFFSET %i"), ResultOffset));
	}

	FString finalQuery = FString::Join(allQueryParams, TEXT(" "));
	return finalQuery;

}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::PrepareStatement(const FString& DatabaseName, const FString& Query, sqlite3** Db, int32** SqlReturnCode,
	sqlite3_stmt** PreparedStatement) {

    const FString* databaseName = Databases.Find(DatabaseName);
    if (!databaseName) {
        LOGSQLITE(Error, TEXT("DB not registered."));
        return false;
    }

    const bool keepOpen = SQLite3Databases.Contains(DatabaseName);
    if (keepOpen) {
        *Db = SQLite3Databases[DatabaseName];
    } else {
        sqlite3_open(TCHAR_TO_ANSI(**databaseName), Db);
    }

	**SqlReturnCode = sqlite3_prepare_v2(*Db, TCHAR_TO_UTF8(*Query), -1, PreparedStatement, NULL);
    return keepOpen;
}

//--------------------------------------------------------------------------------------------------------------

FSQLiteTable USQLiteDatabase::CreateTable(const FString& DatabaseName, const FString& TableName,
	const TArray<FSQLiteTableField> Fields, const FSQLitePrimaryKey PK)
{
	FSQLiteTable t;
	t.DatabaseName = DatabaseName;
	t.TableName = TableName;
	t.Fields = Fields;
	t.PK = PK;

	FString query = "";
	query += "CREATE TABLE IF NOT EXISTS ";
	query += TableName;
	query += "(";

	bool singlePrimaryKeyExists = false;

	for (const FSQLiteTableField& field : Fields)
	{
		if (field.ResultStr.Len() > 2) {

			if (field.ResultStr.Contains("PRIMARY KEY")) {
				singlePrimaryKeyExists = true;
			}

			query += field.ResultStr + ", ";

		}

	}

	if (singlePrimaryKeyExists) {
		query = query.Left(query.Len() - 2);

		query += ");";
	}
	else {
		if (PK.ResultStr.Len() > 2) {
			query += " " + PK.ResultStr + " ";
		}
		else {
			query = query.Left(query.Len() - 2);
		}

		query += ");";
	}

	//LOGSQLITE(Warning, *query);

	t.Created = ExecSql(DatabaseName, query);

	return t;

}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::DropTable(const FString& DatabaseName, const FString& TableName)
{
	return ExecSql(DatabaseName, "DROP TABLE " + TableName);
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::TruncateTable(const FString& DatabaseName, const FString& TableName)
{
	return ExecSql(DatabaseName, "DELETE FROM " + TableName + ";");
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::Vacuum(const FString& DatabaseName)
{
	return ExecSql(DatabaseName, "VACUUM; ");
}

//--------------------------------------------------------------------------------------------------------------
 
/*
int32 USQLiteDatabase::LastInsertRowid(const FString& DatabaseName) {

	sqlite3* db = nullptr;
	const FString* databaseName = Databases.Find(DatabaseName);
	if (!databaseName) {
		LOGSQLITE(Error, TEXT("DB not registered."));
		return -1;
	}

	const bool keepOpen = SQLite3Databases.Contains(DatabaseName);
	if (keepOpen) {
		db = SQLite3Databases[DatabaseName];
	}
	else {
		if (sqlite3_open(TCHAR_TO_ANSI(**databaseName), &db) != SQLITE_OK) {
			LOGSQLITE(Error, TEXT("DB open failed."));
			return -1;
		}
	}

	int32 rowid = (int32)sqlite3_last_insert_rowid(db);

	if (!keepOpen) sqlite3_close(db);

	return rowid;
}
*/

//--------------------------------------------------------------------------------------------------------------
bool USQLiteDatabase::ExecSql(const FString& DatabaseName, const FString& Query) {
	LOGSQLITE(Verbose, *Query);

	sqlite3 *db = nullptr;
    const FString* databaseName = Databases.Find(DatabaseName);
    if (!databaseName) {
        LOGSQLITE(Error, TEXT("DB not registered."));
        return false;
    }

    const bool keepOpen = SQLite3Databases.Contains(DatabaseName);
    if (keepOpen) {
        db = SQLite3Databases[DatabaseName];
    }
    else {
        if (sqlite3_open(TCHAR_TO_ANSI(**databaseName), &db) != SQLITE_OK) {
            LOGSQLITE(Error, TEXT("DB open failed."));
            return false;
        }
    }

    bool success = false;
    char *zErrMsg = nullptr;
    if (sqlite3_exec(db, TCHAR_TO_UTF8(*Query), NULL, 0, &zErrMsg) == SQLITE_OK) {
        success = true;
    } else {
        UE_LOG(LogDatabase, Error, TEXT("SQLite: Query Exec Failed: %s"), UTF8_TO_TCHAR(zErrMsg));
        sqlite3_free(zErrMsg);
    }

    if (!keepOpen) sqlite3_close(db);

    return success;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::CreateIndexes(const FString& DatabaseName, const FString& TableName, const TArray<FSQLiteIndex> Indexes)
{
	bool idxCrSts = true;

	for (const FSQLiteIndex& idx : Indexes)
	{
		if (idx.ResultStr.Len() > 2) {
			FString query = idx.ResultStr.Replace(TEXT("$$$TABLE_NAME$$$"), *TableName);

			//LOGSQLITE(Warning, *query);

			idxCrSts = ExecSql(DatabaseName, query);
			if (!idxCrSts) {
				//LOGSQLITE(Warning, TEXT("ExecSql break"));
				break;
			}
		}

	}

	return idxCrSts;

}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::CreateIndex(const FString& DatabaseName, const FString& TableName, const FSQLiteIndex Index)
{
	return ExecSql(DatabaseName, Index.ResultStr.Replace(TEXT("$$$TABLE_NAME$$$"), *TableName));
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::DropIndex(const FString& DatabaseName, const FString& IndexName)
{
	return ExecSql(DatabaseName, "DROP INDEX " + IndexName);
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::IsTableExists(const FString& DatabaseName, const FString& TableName)
{

	sqlite3* db;
	int32 sqlReturnCode = 0;
	int32* sqlReturnCode1 = &sqlReturnCode;
	sqlite3_stmt* preparedStatement;

	FString Query = "SELECT * FROM sqlite_master WHERE type='table' AND name='" + TableName + "';";

	const bool keepOpen = PrepareStatement(DatabaseName, Query, &db, &sqlReturnCode1, &preparedStatement);
	sqlReturnCode = *sqlReturnCode1;

	if (sqlReturnCode != SQLITE_OK)
	{
		const char* errorMessage = sqlite3_errmsg(db);
		FString error = "SQL error: " + FString(UTF8_TO_TCHAR(errorMessage));
		LOGSQLITE(Error, *error);
		LOGSQLITE(Error, *FString::Printf(TEXT("The attempted query was: %s"), *Query));
		sqlite3_finalize(preparedStatement);
        if (!keepOpen) sqlite3_close(db);
	}

	bool tableExists = false;

	for (sqlReturnCode = sqlite3_step(preparedStatement);
		sqlReturnCode != SQLITE_DONE && sqlReturnCode == SQLITE_ROW;
		sqlReturnCode = sqlite3_step(preparedStatement))
	{
		tableExists = true;
		break;
	}

	//////////////////////////////////////////////////////////////////////////
	// Release the statement and close the connection
	//////////////////////////////////////////////////////////////////////////

	sqlite3_finalize(preparedStatement);
    if (!keepOpen) sqlite3_close(db);

	return tableExists;

}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::InsertRowsIntoTable(const FString& DatabaseName, const FString& TableName, TArray<FSQLiteTableRowSimulator> rowsOfFields){
	bool success = true;

	for (FSQLiteTableRowSimulator row : rowsOfFields) {
		FString query = "INSERT INTO " + TableName + " (";
		for (FSQLiteTableField field : row.rowsOfFields) {
			query += field.FieldName + ", ";
		}

		query = query.Left(query.Len() - 2);

		query = query + ") VALUES (";
		for (FSQLiteTableField field : row.rowsOfFields) {
			if (field.FieldType.Equals(TEXT("TEXT"))) {
				query = query + "'" + field.FieldValue + "', ";
			}
			else {
				query = query + field.FieldValue + ", ";
			}
		}

		query = query.Left(query.Len() - 2);
		query = query + ");";

		//LOGSQLITE(Warning, *query);

		success &= ExecSql(DatabaseName, query);

	}

	return success;
}

//--------------------------------------------------------------------------------------------------------------

bool USQLiteDatabase::UpdateRowsInTable(const FString& DatabaseName, const FString& TableName, TArray<FSQLiteTableRowSimulator> rowsOfFields,
	FSQLiteQueryFinalizedQuery Query, int32 MaxResults, int32 ResultOffset) {
	if (Query.Query.Len() == 0)
	{
		LOGSQLITE(Error, TEXT("The statement needs a where query clause! No operation."));
		return false;
	}

	bool success = true;

	for (FSQLiteTableRowSimulator row : rowsOfFields) {
		FString query = "UPDATE " + TableName + " SET ";

		for (FSQLiteTableField field : row.rowsOfFields) {
			query += field.FieldName + " = ";

			if (field.FieldType.Equals(TEXT("TEXT"))) {
				query = query + "'" + field.FieldValue + "', ";
			}
			else {
				query = query + field.FieldValue + ", ";
			}
		}

		query = query.Left(query.Len() - 2);

		query = query + FString::Printf(TEXT(" WHERE %s"), *Query.Query);

		if (MaxResults >= 0)
		{
			query = query + FString::Printf(TEXT(" LIMIT %i"), MaxResults);
		}

		if (ResultOffset > 0)
		{
			query = query + FString::Printf(TEXT(" OFFSET %i"), ResultOffset);
		}

		query = query + ";";

		//LOGSQLITE(Warning, *query);

		success &= ExecSql(DatabaseName, query);

	}

	return success;
}
//--------------------------------------------------------------------------------------------------------------

void USQLiteDatabase::DeleteRowsInTable(const FString& DatabaseName, const FString& TableName,
	FSQLiteQueryFinalizedQuery Query) {
	if (Query.Query.Len() == 0)
	{
		LOGSQLITE(Error, TEXT("The statement needs a where query clause! No operation."));
		return;
	}

	FString query = "DELETE FROM " + TableName + " WHERE ";

	query = query + FString::Printf(TEXT("%s"), *Query.Query);

	query = query + ";";

	//LOGSQLITE(Warning, *query);

	ExecSql(DatabaseName, query);

}

//--------------------------------------------------------------------------------------------------------------

TUniquePtr<SQLiteQueryResult> USQLiteDatabase::RunQueryAndGetResults(const FString& DatabaseName, const FString& Query)
{
	LOGSQLITE(Verbose, *Query);
    const FString* databaseName = Databases.Find(DatabaseName);
    if (!databaseName) {
        LOGSQLITE(Error, TEXT("DB not registered."));
        return nullptr;
    }

	SQLiteQueryResult result;

	sqlite3* db;
	int32 sqlReturnCode = 0;
	int32* sqlReturnCode1 = &sqlReturnCode;
	sqlite3_stmt* preparedStatement;

	const bool keepOpen = PrepareStatement(DatabaseName, Query, &db, &sqlReturnCode1, &preparedStatement);
	sqlReturnCode = *sqlReturnCode1;

	if (sqlReturnCode != SQLITE_OK)
	{
		const char* errorMessage = sqlite3_errmsg(db);
		FString error = "SQL error: " + FString(UTF8_TO_TCHAR(errorMessage));
		LOGSQLITE(Error, *error);
		LOGSQLITE(Error, *FString::Printf(TEXT("The attempted query was: %s"), *Query));
		result.ErrorMessage = error;
		result.Success = false;
		sqlite3_finalize(preparedStatement);
        if (!keepOpen) sqlite3_close(db);
		return MakeUnique<SQLiteQueryResult>(MoveTemp(result));
	}

	//////////////////////////////////////////////////////////////////////////
	// Get and assign the data
	//////////////////////////////////////////////////////////////////////////

	TArray<SQLiteResultValue> resultRows;

	for (sqlReturnCode = sqlite3_step(preparedStatement);
		sqlReturnCode != SQLITE_DONE && sqlReturnCode == SQLITE_ROW;
		sqlReturnCode = sqlite3_step(preparedStatement))
	{
		SQLiteResultValue row;

		LOGSQLITE(Verbose, TEXT("Query returned a result row."));
		int32 resultColumnCount = sqlite3_column_count(preparedStatement);
		for (int32 c = 0; c < resultColumnCount; c++)
		{
			int32 columnType = sqlite3_column_type(preparedStatement, c);
			const char* columnName = sqlite3_column_name(preparedStatement, c);
			FString columnNameStr = UTF8_TO_TCHAR(columnName);
			SQLiteResultField val;
			val.Name = columnNameStr;
			switch (columnType)
			{
			case SQLITE_INTEGER:
				val.Type = SQLiteResultValueTypes::Integer;
				val.IntValue = sqlite3_column_int64(preparedStatement, c);
				break;
			case SQLITE_TEXT:
				val.Type = SQLiteResultValueTypes::Text;
				val.StringValue = UTF8_TO_TCHAR(sqlite3_column_text(preparedStatement, c));
				break;
			case SQLITE_FLOAT:
				val.Type = SQLiteResultValueTypes::Float;
				val.DoubleValue = sqlite3_column_double(preparedStatement, c);
				break;
			case SQLITE_NULL:
			default:
				val.Type = SQLiteResultValueTypes::UnsupportedValueType;
			}

			if (val.Type != SQLiteResultValueTypes::UnsupportedValueType)
			{
				row.Fields.Add(val);
			}
		}

		resultRows.Add(row);
	}

	//////////////////////////////////////////////////////////////////////////
	// Release the statement and close the connection
	//////////////////////////////////////////////////////////////////////////

	sqlite3_finalize(preparedStatement);
	if (!keepOpen) sqlite3_close(db);

	result.InsertedId = sqlite3_last_insert_rowid(db);
	result.Results = resultRows;
	result.Success = true;
    return MakeUnique<SQLiteQueryResult>(MoveTemp(result));

}

//--------------------------------------------------------------------------------------------------------------

void USQLiteDatabase::AssignResultsToObjectProperties(const SQLiteResultValue& ResultValue, UObject* ObjectToPopulate)
{
	auto propertyMap = CollectProperties(ObjectToPopulate);
	for (SQLiteResultField field : ResultValue.Fields)
	{
		if (propertyMap.Contains(field.Name))
		{
			FProperty* targetProperty = propertyMap[field.Name];

			if (field.Type == SQLiteResultValueTypes::Integer)
			{
				FInt64Property* int64prop = NULL;
				FIntProperty* int32prop = NULL;
				FInt16Property* int16prop = NULL;
				FInt8Property* int8prop = NULL;
				FBoolProperty* boolProp = NULL;

				if ((int64prop = CastField<FInt64Property>(targetProperty)) != NULL)
				{
					int64prop->SetPropertyValue_InContainer(ObjectToPopulate, field.IntValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%d'"), *field.Name, field.IntValue));
				}
				else if ((int32prop = CastField<FIntProperty>(targetProperty)) != NULL)
				{
					int32prop->SetPropertyValue_InContainer(ObjectToPopulate, (int32)field.IntValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%d'"), *field.Name, field.IntValue));
				}
				else if ((int16prop = CastField<FInt16Property>(targetProperty)) != NULL)
				{
					int16prop->SetPropertyValue_InContainer(ObjectToPopulate, (int16)field.IntValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%d'"), *field.Name, field.IntValue));
				}
				else if ((int8prop = CastField<FInt8Property>(targetProperty)) != NULL)
				{
					int8prop->SetPropertyValue_InContainer(ObjectToPopulate, (int8)field.IntValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%d'"), *field.Name, field.IntValue));
				}
				else if ((boolProp = CastField<FBoolProperty>(targetProperty)) != NULL)
				{
					boolProp->SetPropertyValue_InContainer(ObjectToPopulate, field.IntValue > 0);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%d'"), *field.Name, field.IntValue));
				}
			}

			else if (field.Type == SQLiteResultValueTypes::Float)
			{
				FDoubleProperty* doubleProp = NULL;
				FFloatProperty* floatProp = NULL;
				if ((doubleProp = CastField<FDoubleProperty>(targetProperty)) != NULL)
				{
					doubleProp->SetPropertyValue_InContainer(ObjectToPopulate, field.DoubleValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%f'"), *field.Name, field.DoubleValue));
				}
				else if ((floatProp = CastField<FFloatProperty>(targetProperty)) != NULL)
				{
					floatProp->SetPropertyValue_InContainer(ObjectToPopulate, (float)field.DoubleValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%f'"), *field.Name, field.DoubleValue));
				}
			}

			else if (field.Type == SQLiteResultValueTypes::Text)
			{
				FStrProperty* strProp = NULL;
				if ((strProp = CastField<FStrProperty>(targetProperty)) != NULL)
				{
					strProp->SetPropertyValue_InContainer(ObjectToPopulate, field.StringValue);
					LOGSQLITE(Verbose, *FString::Printf(TEXT("Property '%s' was set to '%s'"), *field.Name, *field.StringValue.Mid(0, 64)));
				}
			}

		}
	}
}

//--------------------------------------------------------------------------------------------------------------
