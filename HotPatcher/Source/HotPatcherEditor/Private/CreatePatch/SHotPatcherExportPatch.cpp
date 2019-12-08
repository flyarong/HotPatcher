// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

// #include "HotPatcherPrivatePCH.h"
#include "SHotPatcherExportPatch.h"
#include "FlibHotPatcherEditorHelper.h"
#include "FlibPatchParserHelper.h"
#include "FHotPatcherVersion.h"
#include "FLibAssetManageHelperEx.h"
#include "FFileInfo.h"
// engine header
#include "SHyperlink.h"
#include "Misc/FileHelper.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/SMultiLineEditableText.h"

#include "Kismet/KismetSystemLibrary.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/FileManager.h"

#define LOCTEXT_NAMESPACE "SHotPatcherCreatePatch"

void SHotPatcherExportPatch::Construct(const FArguments& InArgs, TSharedPtr<FHotPatcherCreatePatchModel> InCreatePatchModel)
{

	CreateExportFilterListView();
	mCreatePatchModel = InCreatePatchModel;

	ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(FEditorStyle::GetMargin("StandardDialog.ContentPadding"))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SettingsView->AsShared()
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0, 8.0, 0.0, 0.0)
			
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(4, 4, 10, 4)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("Diff", "Diff"))
					.IsEnabled(this, &SHotPatcherExportPatch::CanDiff)
					.OnClicked(this, &SHotPatcherExportPatch::DoDiff)
					.Visibility(this, &SHotPatcherExportPatch::VisibilityDiffButton)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.Padding(0, 0, 4, 0)
				[
					SNew(SButton)
					.Text(LOCTEXT("ClearDiff", "ClearDiff"))
					.IsEnabled(this, &SHotPatcherExportPatch::CanDiff)
					.OnClicked(this, &SHotPatcherExportPatch::DoClearDiff)
					.Visibility(this, &SHotPatcherExportPatch::VisibilityClearDiffButton)
				]
				+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.Padding(0, 0, 4, 0)
					[
						SNew(SButton)
						.Text(LOCTEXT("GenerateCurrentPatch", "GeneratePatch"))
						.IsEnabled(this, &SHotPatcherExportPatch::CanExportPatch)
						.OnClicked(this, &SHotPatcherExportPatch::DoExportPatch)
					]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.Padding(4, 4, 10, 4)
			[
				SAssignNew(DiffWidget, SHotPatcherInformations)
				.Visibility(EVisibility::Collapsed)
				
			]

		];

	ExportPatchSetting = UExportPatchSettings::Get();
	SettingsView->SetObject(ExportPatchSetting);

}

FReply SHotPatcherExportPatch::DoDiff()const
{
	FString BaseVersionContent;
	FHotPatcherVersion BaseVersion;

	bool bDeserializeStatus = false;

	if (ExportPatchSetting->IsByBaseVersion())
	{
		if (UFLibAssetManageHelperEx::LoadFileToString(ExportPatchSetting->GetBaseVersion(), BaseVersionContent))
		{
			bDeserializeStatus = UFlibPatchParserHelper::DeserializeHotPatcherVersionFromString(BaseVersionContent, BaseVersion);
		}
		if (!bDeserializeStatus)
		{
			UE_LOG(LogTemp, Error, TEXT("Deserialize Base Version Faild!"));
			return FReply::Handled();
		}
	}

	FHotPatcherVersion CurrentVersion = UFlibPatchParserHelper::ExportReleaseVersionInfo(
		ExportPatchSetting->GetVersionId(),
		BaseVersion.VersionId,
		FDateTime::UtcNow().ToString(),
		ExportPatchSetting->GetAssetIncludeFilters(),
		ExportPatchSetting->GetAssetIgnoreFilters(),
		ExportPatchSetting->IsIncludeHasRefAssetsOnly()
	);

	FString CurrentVersionSavePath = FPaths::Combine(ExportPatchSetting->GetSaveAbsPath(), CurrentVersion.VersionId);

	// parser version difference

	FAssetDependenciesInfo BaseVersionAssetDependInfo = BaseVersion.AssetInfo;
	FAssetDependenciesInfo CurrentVersionAssetDependInfo = CurrentVersion.AssetInfo;

	FAssetDependenciesInfo AddAssetDependInfo;
	FAssetDependenciesInfo ModifyAssetDependInfo;
	FAssetDependenciesInfo DeleteAssetDependInfo;

	UFlibPatchParserHelper::DiffVersion(
		CurrentVersionAssetDependInfo,
		BaseVersionAssetDependInfo,
		AddAssetDependInfo,
		ModifyAssetDependInfo,
		DeleteAssetDependInfo
	);

	// FAssetDependenciesInfo AllChangedAssetInfo = UFLibAssetManageHelperEx::CombineAssetDependencies(AddAssetDependInfo, ModifyAssetDependInfo);

	FString SerializeDiffInfo;
	{
		FString AddAssetJsonStr;
		UFLibAssetManageHelperEx::SerializeAssetDependenciesToJson(AddAssetDependInfo, AddAssetJsonStr);
		FString ChangedAssetJsonStr;
		UFLibAssetManageHelperEx::SerializeAssetDependenciesToJson(ModifyAssetDependInfo, ChangedAssetJsonStr);
		FString DeletedAssetJsonStr;
		UFLibAssetManageHelperEx::SerializeAssetDependenciesToJson(DeleteAssetDependInfo, DeletedAssetJsonStr);

		SerializeDiffInfo = FString::Printf(
			TEXT("Add Assets:\n%s\n\nModify Assets:\n%s\n\nDelete Assets:%s\n"),
			*AddAssetJsonStr,
			*ChangedAssetJsonStr,
			*DeletedAssetJsonStr
		);
	}

	DiffWidget->SetContent(SerializeDiffInfo);
	DiffWidget->SetVisibility(EVisibility::Visible);

	return FReply::Handled();
}
bool SHotPatcherExportPatch::CanDiff()const
{
	bool bCanDiff = false;
	if (ExportPatchSetting)
	{
		bool bHasBase = !ExportPatchSetting->GetBaseVersion().IsEmpty() && FPaths::FileExists(ExportPatchSetting->GetBaseVersion());
		bool bHasVersionId = !ExportPatchSetting->GetVersionId().IsEmpty();
		bool bHasFilter = !!ExportPatchSetting->GetAssetIncludeFilters().Num();

		bCanDiff = bHasBase && bHasVersionId && bHasFilter;
	}
	return bCanDiff;
}

FReply SHotPatcherExportPatch::DoClearDiff()const
{
	DiffWidget->SetContent(TEXT(""));
	DiffWidget->SetVisibility(EVisibility::Collapsed);
	return FReply::Handled();
}

EVisibility SHotPatcherExportPatch::VisibilityDiffButton() const
{
	if (CanExportPatch())
	{
		return EVisibility::Visible;
	}
	else {
		return EVisibility::Hidden;
	}
}

EVisibility SHotPatcherExportPatch::VisibilityClearDiffButton() const
{
	if (CanExportPatch())
	{
		return EVisibility::Visible;
	}
	else {
		return EVisibility::Hidden;
	}
}

bool SHotPatcherExportPatch::CanExportPatch()const
{
	bool bCanExport = false;
	if (ExportPatchSetting)
	{
		bool bHasBase = false;
		if (ExportPatchSetting->IsByBaseVersion())
			bHasBase = !ExportPatchSetting->GetBaseVersion().IsEmpty() && FPaths::FileExists(ExportPatchSetting->GetBaseVersion());
		else
			bHasBase = true;
		bool bHasVersionId = !ExportPatchSetting->GetVersionId().IsEmpty();
		bool bHasFilter = !!ExportPatchSetting->GetAssetIncludeFilters().Num();
		bool bHasExternFiles = !!ExportPatchSetting->GetAddExternFiles().Num();

		bool bHasSavePath = !ExportPatchSetting->GetSaveAbsPath().IsEmpty();
		bool bHasPakPlatfotm = !!ExportPatchSetting->GetPakTargetPlatforms().Num();
		bCanExport = bHasBase && bHasVersionId && (bHasFilter || bHasExternFiles) && bHasSavePath;
	}
	return bCanExport;
}

FReply SHotPatcherExportPatch::DoExportPatch()
{
	FString BaseVersionContent;
	FHotPatcherVersion BaseVersion;

	bool bDeserializeStatus = false;

	if(ExportPatchSetting->IsByBaseVersion())
	{
		if (UFLibAssetManageHelperEx::LoadFileToString(ExportPatchSetting->GetBaseVersion(), BaseVersionContent))
		{
			bDeserializeStatus = UFlibPatchParserHelper::DeserializeHotPatcherVersionFromString(BaseVersionContent, BaseVersion);
		}
		if (!bDeserializeStatus)
		{
			UE_LOG(LogTemp, Error, TEXT("Deserialize Base Version Faild!"));
			return FReply::Handled();
		}
	}

	FHotPatcherVersion CurrentVersion = UFlibPatchParserHelper::ExportReleaseVersionInfo(
		ExportPatchSetting->GetVersionId(),
		BaseVersion.VersionId,
		FDateTime::UtcNow().ToString(),
		ExportPatchSetting->GetAssetIncludeFilters(),
		ExportPatchSetting->GetAssetIgnoreFilters(),
		ExportPatchSetting->IsIncludeHasRefAssetsOnly()
	);

	FString CurrentVersionSavePath = FPaths::Combine(ExportPatchSetting->GetSaveAbsPath(), CurrentVersion.VersionId);

	// parser version difference

	FAssetDependenciesInfo BaseVersionAssetDependInfo = BaseVersion.AssetInfo;
	FAssetDependenciesInfo CurrentVersionAssetDependInfo = CurrentVersion.AssetInfo;

	FAssetDependenciesInfo AddAssetDependInfo;
	FAssetDependenciesInfo ModifyAssetDependInfo;
	FAssetDependenciesInfo DeleteAssetDependInfo;

	UFlibPatchParserHelper::DiffVersion(
		CurrentVersionAssetDependInfo,
		BaseVersionAssetDependInfo,
		AddAssetDependInfo,
		ModifyAssetDependInfo,
		DeleteAssetDependInfo
	);

	FAssetDependenciesInfo AllChangedAssetInfo = UFLibAssetManageHelperEx::CombineAssetDependencies(AddAssetDependInfo, ModifyAssetDependInfo);


	float AmountOfWorkProgress = 2.f * ExportPatchSetting->GetPakTargetPlatforms().Num() + 4.0f;
	FScopedSlowTask UnrealPakSlowTask(AmountOfWorkProgress);
	UnrealPakSlowTask.MakeDialog();

	// package all selected platform
	TMap<FString,FFileInfo> PakFilesInfoMap;
	for(const auto &Platform:ExportPatchSetting->GetPakTargetPlatforms())
	{
		// save UnrealPak.exe command file
		FString PlatformName;
		{
			FString EnumName;
			StaticEnum<ETargetPlatform>()->GetNameByValue((int64)Platform).ToString().Split(TEXT("::"), &EnumName, &PlatformName,ESearchCase::CaseSensitive,ESearchDir::FromEnd);
		}
		
		// Update Progress Dialog
		{
			FText Dialog = FText::Format(NSLOCTEXT("ExportPatch", "GeneratedPakCommands", "Generating UnrealPak Commands of {0} Platform."), FText::FromString(PlatformName));
			UnrealPakSlowTask.EnterProgressFrame(1.0, Dialog);
		}

		FString SavePakCommandPath = FPaths::Combine(
			CurrentVersionSavePath,
			PlatformName,
			FString::Printf(TEXT("PakList_%s_%s_%s_PakCommands.txt"), *CurrentVersion.BaseVersionId, *CurrentVersion.VersionId, *PlatformName)
		);

		// combine all cook commands
		{
			FString ProjectDir = UKismetSystemLibrary::GetProjectDirectory();

			// generated cook command form asset list
			TArray<FString> OutPakCommand;
			UFLibAssetManageHelperEx::GetCookCommandFromAssetDependencies(ProjectDir, PlatformName, AllChangedAssetInfo, TArray<FString>{}, OutPakCommand);

			// generated cook command form project ini/AssetRegistry.bin/GlobalShaderCache*.bin
			// and all extern file
			{
				TArray<FString> AllExternCookCommand;

				ExportPatchSetting->GetAllExternAssetCookCommands(ProjectDir, PlatformName, AllExternCookCommand);

				if (!!AllExternCookCommand.Num())
				{
					OutPakCommand.Append(AllExternCookCommand);
				}
				TArray<FString> AllExternFileToPakCommands = ExportPatchSetting->CombineAddExternFileToCookCommands();

				if (!!AllExternFileToPakCommands.Num())
				{
					OutPakCommand.Append(AllExternFileToPakCommands);
				}
			}
			
			// save paklist to file
			if (FFileHelper::SaveStringArrayToFile(OutPakCommand, *SavePakCommandPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
			{
				if (ExportPatchSetting->IsSavePakList())
				{
					auto Msg = LOCTEXT("SavePatchPakCommand", "Succeed to export the Patch Packaghe Pak Command.");
					UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SavePakCommandPath);
				}
			}
		}
		
		// Update SlowTask Progress
		{
			FText Dialog = FText::Format(NSLOCTEXT("ExportPatch", "GeneratedPak", "Generating Pak list of {0} Platform."), FText::FromString(PlatformName));
			UnrealPakSlowTask.EnterProgressFrame(1.0, Dialog);
		}
		// create UnrealPak.exe create .pak file
		{
			FString SavePakFilePath = FPaths::Combine(
				CurrentVersionSavePath,
				PlatformName,
				FString::Printf(TEXT("%s_%s_P.pak"), *CurrentVersion.VersionId, *PlatformName)
			);
			FString UnrealPakBinary = UFlibPatchParserHelper::GetUnrealPakBinary();

			FString CommandLine = FString::Printf(
				TEXT("%s -create=%s"),
				*(TEXT("\"") + SavePakFilePath + TEXT("\"")),
				*(TEXT("\"") + SavePakCommandPath + TEXT("\""))
			);

			// combine UnrealPak Options
			TArray<FString> UnrealPakOptions = ExportPatchSetting->GetUnrealPakOptions();
			for (const auto& Option : UnrealPakOptions)
			{
				CommandLine.Append(FString::Printf(TEXT(" %s"), *Option));
			}

			// create UnrealPak process
			FProcHandle ProcessHandle = FPlatformProcess::CreateProc(*UnrealPakBinary, *CommandLine, true, false, false, NULL, NULL, NULL, NULL);
			FPlatformProcess::WaitForProc(ProcessHandle);

			if (FPaths::FileExists(SavePakFilePath))
			{
				FText Msg = LOCTEXT("SavedPakFileMsg", "Successd to Package the patch as Pak.");
				UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SavePakFilePath);
				
				FFileInfo CurrentPakInfo;
				if (UFlibPatchParserHelper::GetFileInfo(SavePakFilePath, CurrentPakInfo))
				{
					PakFilesInfoMap.Add(PlatformName,CurrentPakInfo);
				}
			}
		}

		// is save PakList?
		if (!ExportPatchSetting->IsSavePakList() && FPaths::FileExists(SavePakCommandPath))
		{
			IFileManager::Get().Delete(*SavePakCommandPath);
		}
	}

	// save difference to file
	{
		{
			FText DiaLogMsg = FText::Format(NSLOCTEXT("ExportPatch","ExportPatchDiffFile","Generating Diff info of version {0}"),FText::FromString(CurrentVersion.VersionId));
			UnrealPakSlowTask.EnterProgressFrame(1.0, DiaLogMsg);
		}
		if (ExportPatchSetting->IsSaveDiffAnalysis())
		{
			FString SerializeDiffInfo;
			UFLibAssetManageHelperEx::SerializeAssetDependenciesToJson(AllChangedAssetInfo, SerializeDiffInfo);

			FString SaveDiffToFile = FPaths::Combine(
				CurrentVersionSavePath,
				FString::Printf(TEXT("%s_%s_Diff.json"), *CurrentVersion.BaseVersionId, *CurrentVersion.VersionId)
			);
			if (UFLibAssetManageHelperEx::SaveStringToFile(SaveDiffToFile, SerializeDiffInfo))
			{
				auto Msg = LOCTEXT("SavePatchDiffInfo", "Succeed to export New Patch Diff Info.");
				UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SaveDiffToFile);
			}
		}
	}

	// save Patch track asset info to file
	{
		{
			FText DiaLogMsg = FText::Format(NSLOCTEXT("ExportPatch", "ExportPatchAssetInfo", "Generating Patch Tacked Asset info of version {0}"), FText::FromString(CurrentVersion.VersionId));
			UnrealPakSlowTask.EnterProgressFrame(1.0, DiaLogMsg);
		}
		FString SerializeCurrentVersionInfo;
		UFlibPatchParserHelper::SerializeHotPatcherVersionToString(CurrentVersion, SerializeCurrentVersionInfo);

		FString SaveCurrentVersionToFile = FPaths::Combine(
			CurrentVersionSavePath,
			FString::Printf(TEXT("%s_Release.json"), *CurrentVersion.VersionId)
		);
		if (UFLibAssetManageHelperEx::SaveStringToFile(SaveCurrentVersionToFile, SerializeCurrentVersionInfo))
		{
			auto Msg = LOCTEXT("SavePatchDiffInfo", "Succeed to export New Release Info.");
			UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SaveCurrentVersionToFile);
		}
	}

	// serialize all pak file info
	{
		{
			FText DiaLogMsg = FText::Format(NSLOCTEXT("ExportPatch", "ExportPatchPakFileInfo", "Generating All Platform Pak info of version {0}"), FText::FromString(CurrentVersion.VersionId));
			UnrealPakSlowTask.EnterProgressFrame(1.0, DiaLogMsg);
		}
		FString PakFilesInfoStr;
		UFlibPatchParserHelper::SerializePlatformPakInfoToString(PakFilesInfoMap, PakFilesInfoStr);

		if (!PakFilesInfoStr.IsEmpty())
		{
			FString SavePakFilesPath = FPaths::Combine(
				CurrentVersionSavePath,
				FString::Printf(TEXT("%s_PakFilesInfo.json"), *CurrentVersion.VersionId)
			);
			if (UFLibAssetManageHelperEx::SaveStringToFile(SavePakFilesPath, PakFilesInfoStr) && FPaths::FileExists(SavePakFilesPath))
			{
				FText Msg = LOCTEXT("SavedPakFileMsg", "Successd to Export the Pak File info.");
				UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SavePakFilesPath);
			}
		}
	}

	// serialize patch config
	{
		{
			FText DiaLogMsg = FText::Format(NSLOCTEXT("ExportPatch", "ExportPatchConfig", "Generating Current Patch Config of version {0}"), FText::FromString(CurrentVersion.VersionId));
			UnrealPakSlowTask.EnterProgressFrame(1.0, DiaLogMsg);
		}

		FString SaveConfigPath = FPaths::Combine(
			CurrentVersionSavePath,
			FString::Printf(TEXT("%s_PatchConfig.json"),*CurrentVersion.VersionId)
		);

		if (ExportPatchSetting->IsSavePatchConfig())
		{
			FString SerializedJsonStr;
			ExportPatchSetting->SerializePatchConfigToString(SerializedJsonStr);

			if (FFileHelper::SaveStringToFile(SerializedJsonStr, *SaveConfigPath))
			{
				FText Msg = LOCTEXT("SavedPatchConfigMas", "Successd to Export the Patch Config.");
				UFlibHotPatcherEditorHelper::CreateSaveFileNotify(Msg, SaveConfigPath);
			}
		}
	}

	return FReply::Handled();
}

void SHotPatcherExportPatch::CreateExportFilterListView()
{
	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs;
	DetailsViewArgs.bUpdatesFromSelection = true;
	DetailsViewArgs.bLockable = true;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ComponentsAndActorsUseNameArea;
	DetailsViewArgs.bCustomNameAreaLocation = false;
	DetailsViewArgs.bCustomFilterAreaLocation = true;
	DetailsViewArgs.DefaultsOnlyVisibility = EEditDefaultsOnlyNodeVisibility::Hide;

	SettingsView = EditModule.CreateDetailView(DetailsViewArgs);
}


#undef LOCTEXT_NAMESPACE
