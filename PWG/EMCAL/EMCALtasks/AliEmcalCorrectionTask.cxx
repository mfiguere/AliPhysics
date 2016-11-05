// AliEmcalCorrectionTask
//
// Runs the cell and cluster level corrections for the EMCal
//

#include "AliEmcalCorrectionTask.h"
#include "AliEmcalCorrectionComponent.h"

#include <vector>
#include <set>
#include <fstream>
#include <sstream>
#include <iostream>

#include <TChain.h>
#include <TSystem.h>
#include <TGrid.h>
#include <TFile.h>

#include "AliVEventHandler.h"
#include "AliEMCALGeometry.h"
#include "AliVCaloCells.h"
#include "AliVCluster.h"
#include "AliLog.h"
#include "AliMultSelection.h"
#include "AliCentrality.h"
#include "AliESDEvent.h"
#include "AliAnalysisManager.h"

/// \cond CLASSIMP
ClassImp(AliEmcalCorrectionTask);
/// \endcond

/// \cond CLASSIMP
ClassImp(AliEmcalCorrectionCellContainer);
/// \endcond

/**
 *
 *
 */
AliEmcalCorrectionTask::AliEmcalCorrectionTask() :
  AliAnalysisTaskSE("AliEmcalCorrectionTask"),
  fUserConfiguration(),
  fUserConfigurationFilename(""),
  fUserConfigurationString(""),
  fDefaultConfiguration(),
  fDefaultConfigurationFilename(""),
  fDefaultConfigurationString(""),
  fCorrectionComponents(),
  fIsEsd(false),
  fForceBeamType(kNA),
  fRunPeriod(""),
  fConfigurationInitialized(false),

  fEventInitialized(false),
  fCent(0),
  fCentBin(-1),
  fMinCent(-999),
  fMaxCent(-999),
  fNcentBins(4),
  fCentEst("V0M"),
  fUseNewCentralityEstimation(kFALSE),
  fNVertCont(0),
  fBeamType(kNA),
  fNeedEmcalGeom(kTRUE),
  fGeom(0),
  fParticleCollArray(),
  fClusterCollArray(),
  fCellCollArray(),
  fOutput(0)
{
  // Default constructor
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));

  fVertex[0] = 0;
  fVertex[1] = 0;
  fVertex[2] = 0;

  fParticleCollArray.SetOwner(kTRUE);
  fClusterCollArray.SetOwner(kTRUE);
}

/**
 *
 *
 */
AliEmcalCorrectionTask::AliEmcalCorrectionTask(const char * name) :
  AliAnalysisTaskSE(name),
  fUserConfiguration(),
  fUserConfigurationFilename(""),
  fUserConfigurationString(""),
  fDefaultConfiguration(),
  fDefaultConfigurationFilename(""),
  fDefaultConfigurationString(""),
  fCorrectionComponents(),
  fIsEsd(false),
  fForceBeamType(kNA),
  fRunPeriod(""),
  fConfigurationInitialized(false),

  fEventInitialized(false),
  fCent(0),
  fCentBin(-1),
  fMinCent(-999),
  fMaxCent(-999),
  fNcentBins(4),
  fCentEst("V0M"),
  fUseNewCentralityEstimation(kFALSE),
  fNVertCont(0),
  fBeamType(kNA),
  fNeedEmcalGeom(kTRUE),
  fGeom(0),
  fParticleCollArray(),
  fClusterCollArray(),
  fCellCollArray(),
  fOutput(0)
{
  // Standard constructor
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));

  fVertex[0] = 0;
  fVertex[1] = 0;
  fVertex[2] = 0;

  fParticleCollArray.SetOwner(kTRUE);
  fClusterCollArray.SetOwner(kTRUE);

  DefineInput(0, TChain::Class());
  DefineOutput(1, TList::Class());
}

/** Checks if a file exists. This is done inline to make it efficient.
 * See: https://stackoverflow.com/a/19841704
 *
 * \param filename String containing the filename of the file to check.
 *
 * \return True if the file exists.
 */
inline bool AliEmcalCorrectionTask::DoesFileExist(const std::string & filename)
{
  std::ifstream inFile(filename);
  return inFile.good();
}

/**
 * Handles expanding ALICE_PHYSICS and copying the file from the grid if necessary.
 *
 */
void AliEmcalCorrectionTask::SetupConfigurationFilePath(std::string & filename, bool userFile)
{
  if (filename != "")
  {
    // Handle if in AliPhysics
    // Check for and replace $ALICE_PHYSICS with the actual path if needed
    std::size_t alicePhysicsPathLocation = filename.find("$ALICE_PHYSICS");
    if (alicePhysicsPathLocation != std::string::npos)
    {
      TString alicePhysicsPath = gSystem->Getenv("ALICE_PHYSICS");
      // "$ALICE_PHYSICS "is 14 characters
      filename.replace(alicePhysicsPathLocation, alicePhysicsPathLocation + 14, alicePhysicsPath.Data());
    }

    // Handle grid
    if(filename.find("alien://") != std::string::npos)
    {
      AliDebug(2, TString::Format("Opening file \"%s\" on the grid!", filename.c_str()));
      // Init alien connection if needed
      if (!gGrid) {
        TGrid::Connect("alien://");
      }

      // Determine the loca filename and copy file to local directory
      std::string localFilename = gSystem->BaseName(filename.c_str());
      // Ensures that the default and user files do not conflict if both are taken from the grid and have the same filename
      if (userFile == true) {
        localFilename = "user" + localFilename;
      }
      TFile::Cp(filename.c_str(), localFilename.c_str());

      // yaml-cpp should only open the local file
      filename = localFilename;
    }
  }
}


/**
 *
 *
 */
void AliEmcalCorrectionTask::InitializeConfiguration()
{
  // Determine file path
  if (fDefaultConfigurationFilename == "")
  {
    // Use the default if nothing is set
    fDefaultConfigurationFilename = "$ALICE_PHYSICS/PWG/EMCAL/config/AliEmcalCorrectionConfiguration.yaml";
  }

  // Setup the YAML files
  // Default
  SetupConfigurationFilePath(fDefaultConfigurationFilename);

  if (DoesFileExist(fDefaultConfigurationFilename) == true)
  {
    AliInfo(TString::Format("Using default EMCal corrections configuration located at %s", fDefaultConfigurationFilename.c_str()));

    fDefaultConfiguration = YAML::LoadFile(fDefaultConfigurationFilename);
    // Check for valid file
    if (fDefaultConfiguration.IsNull() == true)
    {
      AliFatal(TString::Format("Could not open the default configuration file \"%s\"!", fDefaultConfigurationFilename.c_str()));
    }
  }
  else
  {
    AliFatal(TString::Format("Default file located at \"%s\" does not exist!", fDefaultConfigurationFilename.c_str()));
  }

  // User
  SetupConfigurationFilePath(fUserConfigurationFilename, true);

  if (DoesFileExist(fUserConfigurationFilename) == true)
  {
    AliInfo(TString::Format("Using user EMCal corrections configuration located at %s", fUserConfigurationFilename.c_str()));

    fUserConfiguration = YAML::LoadFile(fUserConfigurationFilename);
  }
  else
  {
    AliInfo(TString::Format("User file at \"%s\" does not exist! All settings will be from the default file!", fUserConfigurationFilename.c_str()));
  }

  // Ensure that there is a run period
  if (fRunPeriod == "")
  {
    AliFatal("Must pass a run period to the correction task!");
  }
  // Check the user provided run period
  TString userRunPeriod = "kNoUserFile";
  // Test if the period exists in the user file
  if (fUserConfiguration.IsNull() != true)
  {
    if (fUserConfiguration["period"])
    {
      userRunPeriod = fUserConfiguration["period"].as<std::string>();
    }
    else
    {
      AliFatal("User must specify a period. Leave the period as an empty string to apply to all periods.");
    }
  }

  AliDebug(3, TString::Format("userRunPeriod: %s", userRunPeriod.Data()));
  // Normalize the user run period to lower case to ensure that we don't miss any matches
  userRunPeriod.ToLower();
  // "" means the user wants their settings to apply to all periods
  if (userRunPeriod != "" && userRunPeriod != "knouserfile" && userRunPeriod != fRunPeriod)
  {
    AliFatal(TString::Format("User run period \"%s\" does not match the run period of \"%s\" passed to the correction task!", userRunPeriod.Data(), fRunPeriod.Data()));
  }

  // Ensure that the user is aware
  if (userRunPeriod == "")
  {
    AliWarning("User run period is an empty string. Settings apply to all run periods!");
  }

  // Save configuration into strings so that they can be streamed
  // Need the stringstream because yaml implements streamers
  std::stringstream tempConfiguration;
  tempConfiguration << fUserConfiguration;
  fUserConfigurationString = tempConfiguration.str();
  tempConfiguration.str("");
  tempConfiguration << fDefaultConfiguration;
  fDefaultConfigurationString = tempConfiguration.str();

  //AliInfo(TString::Format("User configuration: %s", fUserConfigurationString.c_str()));
  //AliInfo(TString::Format("Default configuration: %s", fDefaultConfigurationString.c_str()));

  // Note that it is initialized properly so that the analysis can proceed
  fConfigurationInitialized = true;
}

/**
 * Writes the desired yaml configuration to a file.
 * 
 * \param filename The name of the file to write.
 * \param userCofig True to write the user configuration.
 * \return Whether writing the configuration to the file was successful.
 */
bool AliEmcalCorrectionTask::WriteConfigurationFile(std::string filename, bool userConfig)
{
  bool returnValue = false;
  if (filename != "")
  {
    if (fConfigurationInitialized == true)
    {
      std::ofstream outFile(filename);
      std::string stringToWrite = userConfig ? fUserConfigurationString : fDefaultConfigurationString;
      if (stringToWrite == "") {
        AliWarning(TString::Format("%s configuration is empty!", userConfig ? "User" : "Default"));
      }
      outFile << stringToWrite;
      outFile.close();

      returnValue = true;
    }
    else
    {
      AliWarning(TString::Format("Configuration not properly initialized! Cnanot print %s configuration!", userConfig ? "user" : "default"));
    }

  }
  else
  {
    AliWarning("Please pass a valid filename instead of empty qutoes!");
  }
  return returnValue;
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::Initialize()
{
  // Determine file type
  AliAnalysisManager *mgr = AliAnalysisManager::GetAnalysisManager();
  if (mgr) {
    AliVEventHandler *evhand = mgr->GetInputEventHandler();
    if (evhand) {
      if (evhand->InheritsFrom("AliESDInputHandler")) {
        fIsEsd = true;
      }
      else {
        fIsEsd = false;
      }
    }
    else {
      AliError("Event handler not found!");
    }
  }
  else {
    AliError("Analysis manager not found!");
  }
  
  // Initialize YAML configuration
  InitializeConfiguration();
  // Check that the configuration is initialized
  if (fConfigurationInitialized != true)
  {
    AliFatal("YAML configuration must be initialized before running (ie. the AddTask, run macro or wagon)!");
  }

  // Setup input objects
  // Setup Cells
  // Cannot do this entirely yet because we need input objects
  CreateInputObjects(kCaloCells);
  // TEMP PRINT
  AliDebugStream(2) << "Cell info: " << std::endl;
  for (auto cellInfo : fCellCollArray) {
    AliDebugStream(2) << "\tName: " << cellInfo->GetName() << "\tBranch: " << cellInfo->GetBranchName() << "\tIsEmbedding:" << cellInfo->GetIsEmbedding() << std::endl;
  }
  // END TEMP PRINT
  // Create cluster input objects
  CreateInputObjects(kCluster);
  // TEMP PRINT
  fClusterCollArray.Print();
  // END TEMP PRINT
  // Create track input objects
  CreateInputObjects(kTrack);
  // TEMP PRINT
  fParticleCollArray.Print();
  // END TEMP PRINT

  // Initialize components
  InitializeComponents();
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::RetrieveExecutionOrder(std::vector <std::string> & executionOrder)
{
  AliEmcalCorrectionComponent::GetProperty("executionOrder", executionOrder, fUserConfiguration, fDefaultConfiguration, true);
  // Need to append "AliEmcalCorrection" to allow the tasks to be found!
  AliDebug(2, "Creating EMCal Correction Components: ");
  for (auto & component : executionOrder)
  {
    component = "AliEmcalCorrection" + component;
    AliDebug(2, TString::Format("%s", component.c_str()) );
  }
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::InitializeComponents()
{
  // Create a function to handle creation and configuration of the all of the created module
  std::vector<std::string> executionOrder;
  RetrieveExecutionOrder(executionOrder);

  // Iterate over the list
  AliEmcalCorrectionComponent * component = 0;
  bool componentEnabled = false;
  for (auto componentName : executionOrder)
  {
    componentEnabled = false;
    AliEmcalCorrectionComponent::GetProperty("enabled", componentEnabled, fUserConfiguration, fDefaultConfiguration, true, componentName);
    if (componentEnabled == false)
    {
      AliInfo(TString::Format("Component %s is disabled and will not be run!", componentName.c_str()));
      continue;
    }

    //Printf("Attempting to add task: %s", tempString->GetString().Data());
    component = AliEmcalCorrectionComponentFactory::createInstance(componentName);
    if (!component)
    {
      AliFatal(TString::Format("Failed to create requested component %s!", componentName.c_str()));
    }

    // For setting names of tasks to differentiate between tasks of the same class
    component->SetName(componentName.c_str());
    component->SetTitle(componentName.c_str());

    // Initialize the YAML configurations in each component
    component->SetUserConfiguration(fUserConfiguration);
    component->SetDefaultConfiguration(fDefaultConfiguration);

    // configure needed fields for components to properly initialize
    component->SetIsESD(fIsEsd);

    // Add the require containers to the component
    // Cells must be set during UserExec() because we need to add them as a pointer
    AddContainersToComponent(component, kCluster);
    AddContainersToComponent(component, kTrack);

    // Initialize each component
    component->Initialize();

    if (component)
    {
      AliInfo(TString::Format("Successfully added correction task: %s", componentName.c_str()));
      fCorrectionComponents.push_back(component);
    }
  }
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::UserCreateOutputObjects()
{
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));

  // Check that the configuration is initialized
  if (fConfigurationInitialized != true)
  {
    AliFatal("YAML configuration must be initialized before running (ie. the AddTask, run macro or wagon)!");
  }

  // Show the configurations info this is available
  AliDebugStream(4) << "User configuration string: " << fUserConfigurationString << std::endl;
  AliDebugStream(4) << "User configuration: " << fUserConfiguration << std::endl;
  AliDebugStream(4) << "Default configuration string: " << fDefaultConfigurationString << std::endl;
  AliDebugStream(4) << "Default configuration: " << fDefaultConfiguration << std::endl;

  // YAML Objects cannot be streamed, so we need to reinitialize them here.
  // They need reinitialize if they are null
  if (fUserConfiguration.IsNull() == true && fUserConfigurationString != "")
  {
    AliInfo("Reinitializing user configuration from string. Expected if running on grid!");
    fUserConfiguration = YAML::Load(fUserConfigurationString);
  }
  if (fDefaultConfiguration.IsNull() == true)
  {
    AliInfo("Reinitializing default configuration from string. Expected if running on grid!");
    fDefaultConfiguration = YAML::Load(fDefaultConfigurationString);
  }

  // Debug to check that the configuration has been (re)initiailzied has been completed correctly
  AliDebugStream(4) << "(Re)initialized user configuration: " << fUserConfigurationString << std::endl;
  AliDebugStream(4) << "(Re)initialized user configuration: " << fUserConfiguration << std::endl;
  AliDebugStream(4) << "(Re)initialized default configuration: " << fDefaultConfigurationString << std::endl;
  AliDebugStream(4) << "(Re)initialized default configuration: " << fDefaultConfiguration << std::endl;

  if (fForceBeamType == kpp)
    fNcentBins = 1;

  // Allow for output files
  OpenFile(1);
  fOutput = new TList();
  fOutput->SetOwner();

  UserCreateOutputObjectsComponents();

  PostData(1, fOutput);
}

/**
 * Calls UserCreateOutputObjects() for each component.
 *
 */
void AliEmcalCorrectionTask::UserCreateOutputObjectsComponents()
{
  // Run the initialization for all derived classes.
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  for (auto component : fCorrectionComponents)
  {
    // Set cent bins (usually used for hist creation)
    // It cannot be set until now because it can be changed after initialization
    // For instance, by SetForceBeamType()
    component->SetNcentralityBins(fNcentBins);

    component->UserCreateOutputObjects();

    if (component->GetOutputList() != 0)
    {
      // Adds a list to the list -- this doesn't work for some unknown reason
      //fOutput->Add(component->GetOutputList());

      // iterate through lists for each component, and fill in output
      TList* t = new TList();
      t->SetName(component->GetName());
      fOutput->AddLast(t);
      t = (TList*)fOutput->Last();
      TIter next(component->GetOutputList());
      while (TObject *obj = next()){
        t->Add(obj);
      }

      AliDebug(1, TString::Format("Added output list from task %s to output.", component->GetName()));
    }
  }
}

/**
 * Given the input type, return the name of the field in the YAML file where information about it
 * should be lcoated.
 */
std::string AliEmcalCorrectionTask::GetInputFieldNameFromInputObjectType(InputObject_t inputObjectType)
{
  // Get container node
  std::string inputObjectName = "";
  if (inputObjectType == kCluster) {
    inputObjectName = "clusterContainers";
  }
  else if (inputObjectType == kTrack) {
    inputObjectName = "trackContainers";
  }
  else if (inputObjectType == kCaloCells) {
    inputObjectName = "cells";
  }
  else {
    AliFatal(TString::Format("Unrecognized input object type %d", inputObjectType));
  }

  return inputObjectName;
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::GetNodeForInputObjects(YAML::Node & inputNode, YAML::Node & nodeToRetrieveFrom, std::string & inputObjectName, bool requiredProperty)
{
  // Get the user input node
  AliEmcalCorrectionComponent::GetProperty(inputObjectName.c_str(), inputNode, YAML::Node(), nodeToRetrieveFrom, requiredProperty, "inputObjects");

  // Get the user shared node and add it back to the user node so that shared parameters are available
  if (nodeToRetrieveFrom["sharedParameters"]) {
    inputNode["sharedParameters"] = nodeToRetrieveFrom["sharedParameters"];
  }
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::SetupContainersFromInputNodes(InputObject_t inputObjectType, YAML::Node & userInputObjectNode, YAML::Node & defaultInputObjectNode, std::set <std::string> & requestedContainers)
{
  // Our node contains all of the objects that we will want to create.
  for(auto & containerName : requestedContainers)
  {
    // The section is the container name
    //std::string containerName = it->first.as<std::string>();
    // Skip over the sharedParamters node
    // Also skip if the particle or cluster container already exists
    if (containerName == "sharedParameters" || GetParticleContainer(containerName.c_str()) || GetClusterContainer(containerName.c_str())) {
      continue;
    }

    AliDebug(2, TString::Format("Processing container %s of inputType %d", containerName.c_str(), inputObjectType));
    //std::cout << "Container: " << containerName << std::endl << userInputObjectNode << std::endl << defaultInputObjectNode;
    if (inputObjectType == kCluster || inputObjectType == kTrack) {
      SetupContainer(inputObjectType, containerName, userInputObjectNode, defaultInputObjectNode);
    }
    else if (inputObjectType == kCaloCells) {
      SetupCellsInfo(containerName, userInputObjectNode, defaultInputObjectNode);
    }
  }
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::SetupCellsInfo(std::string containerName, YAML::Node & userNode, YAML::Node & defaultNode)
{
  // Define cell info
  AliEmcalCorrectionCellContainer * cellObj = new AliEmcalCorrectionCellContainer();

  AliDebugStream(2) << "User: " << std::endl << userNode << std::endl << "default: " << std::endl << defaultNode << std::endl;

  // Set properties
  // Cells (object) name
  cellObj->SetName(containerName);
  // Branch name
  std::string tempString = "";
  AliEmcalCorrectionComponent::GetProperty("branchName", tempString, userNode, defaultNode, true, containerName);
  if (tempString == "usedefault") {
    tempString = DetermineUseDefaultName(kCaloCells, fIsEsd);
  }
  cellObj->SetBranchName(tempString);
  // Where to copy the branch from
  /*AliEmcalCorrectionComponent::GetProperty("branchToCopy", tempString, userNode, defaultNode, false, containerName);
  cellObj->SetBranchToCopyName(tempString);*/

  // IsEmbedding
  bool tempBool = false;
  AliEmcalCorrectionComponent::GetProperty("embedded", tempString, userNode, defaultNode, false, containerName);
  cellObj->SetIsEmbedding(tempBool);

  // Add to the array to keep track of it
  fCellCollArray.push_back(cellObj);
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::CreateInputObjects(InputObject_t inputObjectType)
{
  // Get container node
  std::string inputObjectName = GetInputFieldNameFromInputObjectType(inputObjectType);

  // Get the user and default input nodes for the object type
  YAML::Node userInputObjectNode;
  YAML::Node defaultInputObjectNode;
  GetNodeForInputObjects(userInputObjectNode, fUserConfiguration, inputObjectName, false);
  GetNodeForInputObjects(defaultInputObjectNode, fDefaultConfiguration, inputObjectName, true);

  AliDebugStream(2) << "userInputObjectNode: " << userInputObjectNode << std::endl;
  AliDebugStream(2) << "defaultInputObjectNode: " << defaultInputObjectNode << std::endl;

  // Determine which containers we need based on which are requested by correction tasks
  std::set <std::string> requestedContainers;
  std::vector <std::string> executionOrder;
  std::vector <std::string> componentRequest;
  RetrieveExecutionOrder(executionOrder);
  for ( auto & componentName : executionOrder )
  {
    bool componentEnabled = false;
    AliEmcalCorrectionComponent::GetProperty("enabled", componentEnabled, fUserConfiguration, fDefaultConfiguration, true, componentName);
    if (componentEnabled)
    {
      componentRequest.clear();
      // Not required because not all components will have all kinds of containers
      AliEmcalCorrectionComponent::GetProperty(inputObjectName + "Names", componentRequest, fUserConfiguration, fDefaultConfiguration, false, componentName);
      for ( auto & req : componentRequest )
      {
        //std::cout << "Component " << componentName << " requested container name " << req << std::endl;
        requestedContainers.insert(req);
      }
    }
  }

  AliInfoStream() << inputObjectName << " Containers requested by components: " << std::endl;
  for (auto & str : requestedContainers) {
    AliInfoStream() << "\t" << str << std::endl;;
  }

  // Create all requested containers
  AliDebug(2, TString::Format("Setting up requested containers!"));
  SetupContainersFromInputNodes(inputObjectType, userInputObjectNode, defaultInputObjectNode, requestedContainers);
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::SetupContainer(InputObject_t inputObjectType, std::string containerName, YAML::Node & userNode, YAML::Node & defaultNode)
{
  // Create container
  AliDebugStream(2) << "Adding container" << std::endl;
  AliEmcalContainer * cont = AddContainer(inputObjectType, containerName, userNode, defaultNode);
  AliDebugStream(2) << "Added container" << std::endl;

  // Set the container properties
  //
  // TODO: Consider if this can be converted to a map to function pointers. There are a number of details which can make it a bit complicated.
  //       Those details include inheritance, pointing to member functions, etc. It should all be possible, but may not be worth all of the extra
  //       work and code.
  //       Example ccode:
  //          SetValueInContainer("minPt", &cont::SetMinPt, tempDouble, userNode, defaultNode);
  //          SetValueInContainer("minE", &cont::SetMinE, tempDouble, userNode, defaultNode);

  // Temporary variables to store requested properties
  std::string tempString = "";
  Double_t tempDouble = 0;
  bool tempBool = false;

  // AliEmcalContainer properties
  // Min Pt
  bool result = AliEmcalCorrectionComponent::GetProperty("minPt", tempDouble, userNode, defaultNode, false, containerName);
  if (result) {
    AliDebugStream(2) << cont->GetName() << ": Setting minPt of " << tempDouble << std::endl;
    cont->SetMinPt(tempDouble);
  }
  // Min E
  result = AliEmcalCorrectionComponent::GetProperty("minE", tempDouble, userNode, defaultNode, false, containerName);
  if (result) {
    AliDebugStream(2) << cont->GetName() << ": Setting minE of " << tempDouble << std::endl;
    cont->SetMinE(tempDouble);
  }
  // Eta min, max
  result = AliEmcalCorrectionComponent::GetProperty("minEta", tempDouble, userNode, defaultNode, false, containerName);
  if (result) {
    // Only continue checking if the min is there, since we must set both together
    Double_t tempDouble2 = 0;
    result = AliEmcalCorrectionComponent::GetProperty("maxEta", tempDouble, userNode, defaultNode, false, containerName);
    if (result) {
      AliDebugStream(2) << cont->GetName() << ": Setting eta limits of " << tempDouble << " to " << tempDouble2 << std::endl;
      cont->SetEtaLimits(tempDouble, tempDouble2);
    }
  }
  // Phi min, max
  result = AliEmcalCorrectionComponent::GetProperty("minPhi", tempDouble, userNode, defaultNode, false, containerName);
  if (result) {
    // Only continue checking if the min is there, since we must set both together
    Double_t tempDouble2 = 0;
    result = AliEmcalCorrectionComponent::GetProperty("maxPhi", tempDouble, userNode, defaultNode, false, containerName);
    if (result) {
      AliDebugStream(2) << cont->GetName() << ": Setting phi limits of " << tempDouble << " to " << tempDouble2 << std::endl;
      cont->SetPhiLimits(tempDouble, tempDouble2);
    }
  }
  // Embedded
  // TODO: Enable embedded when that branch is committed!
  /*result = AliEmcalCorrectionComponent::GetProperty("IsEmbedded", tempBool, userNode, defaultNode, false, containerName);
  if (result) {
    AliDebugStream(2) << cont->GetName() << ": Setting embedding to " << (tempBool ? "enabled" : "disabled") << std::endl;
    cont->SetIsEmbedding(tempBool);
  }*/

  // Cluster specific properties
  AliClusterContainer * clusterContainer = dynamic_cast<AliClusterContainer *>(cont);
  if (clusterContainer) {
    // Default energy
    // Probably not needed for the corrections
    /*result = AliEmcalCorrectionComponent::GetProperty("defaultClusterEnergy", tempString, userNode, defaultNode, false, containerName);
    if (result) {
      // Need to get the enumeration
      AliVCluster::VCluUserDefEnergy_t clusterEnergyType = clusterEnergyTypeMap.at(tempString);
      AliDebugStream(2) << clusterContainer->GetName() << ": Setting cluster energy type to " << clusterEnergyType << std::endl;
      clusterContainer->SetDefaultClusterEnergy(clusterEnergyType);
    }*/

    // NonLinCorrEnergyCut
    result = AliEmcalCorrectionComponent::GetProperty("clusNonLinCorrEnergyCut", tempDouble, userNode, defaultNode, false, containerName);
    if (result) {
      AliDebugStream(2) << clusterContainer->GetName() << ": Setting clusNonLinCorrEnergyCut of " << tempDouble << std::endl;
      clusterContainer->SetClusNonLinCorrEnergyCut(tempDouble);
    }

    // HadCorrEnergyCut
    result = AliEmcalCorrectionComponent::GetProperty("clusHadCorrEnergyCut", tempDouble, userNode, defaultNode, false, containerName);
    if (result) {
      AliDebugStream(2) << clusterContainer->GetName() << ": Setting clusHadCorrEnergyCut of " << tempDouble << std::endl;
      clusterContainer->SetClusHadCorrEnergyCut(tempDouble);
    }
  }

  // Track specific
  AliTrackContainer * trackContainer = dynamic_cast<AliTrackContainer *>(cont);
  if (trackContainer) {
    // Track selection
    // AOD Filter bits as a sequence
    std::vector <UInt_t> filterBitsVector;
    result = AliEmcalCorrectionComponent::GetProperty("aodFilterBits", filterBitsVector, userNode, defaultNode, false, containerName);
    if (result){
      UInt_t filterBits = 0;
      for (int filterBit : filterBitsVector) {
        filterBits += filterBit;
      }
      AliDebugStream(2) << trackContainer->GetName() << ": Setting filterBits of " << filterBits << std::endl;
      trackContainer->SetAODFilterBits(filterBits);
    }

    // SetTrackFilterType enum
    result = AliEmcalCorrectionComponent::GetProperty("trackFilterType", tempString, userNode, defaultNode, false, containerName);
    if (result) {
      // Need to get the enumeration
      AliEmcalTrackSelection::ETrackFilterType_t trackFilterType = trackFilterTypeMap.at(tempString);
      AliDebugStream(2) << trackContainer->GetName() << ": Setting trackFilterType of " << trackFilterType << std::endl;
      trackContainer->SetTrackFilterType(trackFilterType);
    }
  }
}

/**
 * Given a container type, it returns the proper branch name based on the "usedefault" pattern.
 * If returnObjectType is true, it returns the "default" (unlikely to change) object type given
 * the input object type and the file mode.
 */
std::string AliEmcalCorrectionTask::DetermineUseDefaultName(InputObject_t objType, bool esdMode, bool returnObjectType)
{
  std::string returnValue = "";
  if (objType == kCluster) {
    if (esdMode == true) {
      if (returnObjectType == true) {
        returnValue = "AliESDCaloCluster";
      }
      else {
        returnValue = "CaloClusters";
      }
    }
    else {
      if (returnObjectType == true) {
        returnValue = "AliAODCaloCluster";
      }
      else {
        returnValue = "caloClusters";
      }
    }
  }
  else if (objType == kTrack) {
    if (esdMode == true) {
      if (returnObjectType == true) {
        returnValue = "AliESDtrack";
      }
      else {
        returnValue = "Tracks";
      }
    }
    else {
      if (returnObjectType == true) {
        returnValue = "AliAODTrack";
      }
      else {
        returnValue = "tracks";
      }
    }
  }
  else if (objType == kCaloCells) {
    if (esdMode == true) {
      if (returnObjectType == true) {
        returnValue = "AliESDCaloCells";
      }
      else {
        returnValue = "EMCALCells";
      }
    }
    else {
      if (returnObjectType == true) {
        returnValue = "AliAODCaloCells";
      }
      else {
        returnValue = "emcalCells";
      }
    }
  }
  else {
    // Default to empty if we are given an unrecognized type with "usedefault"
    returnValue = "";
  }

  return returnValue;
}

/**
 * Creates a new container based on the requested type and the branch name set in
 * the configuration file. Suppports the "usedefault" pattern to simplify setting
 * the proper branch name.
 *
 * Note: Adding a container using this function also sets the container variable
 * (for example, fPartCont for a particle container), so it can be used immediately
 * after this function is called.
 *
 * @param[in] contType Type of container to be created
 * @return The created container
 *
 */
AliEmcalContainer * AliEmcalCorrectionTask::AddContainer(InputObject_t contType, std::string & containerName, YAML::Node & userNode, YAML::Node & defaultNode)
{
  // Determine the type of branch to request
  std::string containerBranch = "";
  if (contType != kCluster && contType != kTrack){
    AliFatal("Must specify type of container when requesting branch.");
  }

  // Retrieve branch name
  // YAML::Node() is just an empty node
  AliDebugStream(2) << "User Node: " << userNode << std::endl;
  AliDebugStream(2) << "Default Node: " << defaultNode << std::endl;
  AliEmcalCorrectionComponent::GetProperty("branchName", containerBranch, userNode, defaultNode, true, containerName);
  // Should be unnecessary, since the user can only do this if done explicitly.
  /*if (containerBranch == "")
  {
    AliFatal(TString::Format("Request %i container, but the container branch is empty!", contType));
  }*/

  // Determine proper name if using "usedefault" pattern
  if (containerBranch == "usedefault") {
    containerBranch = DetermineUseDefaultName(contType, fIsEsd);
  }

  // Create containers and set them to the name of the component
  AliEmcalContainer * cont = 0;
  if (contType == kCluster)
  {
    cont = new AliClusterContainer(containerBranch.c_str());
    AdoptClusterContainer(dynamic_cast<AliClusterContainer *>(cont));
  }
  else if (contType == kTrack)
  {
    if (containerBranch == "mcparticles") {
      cont = new AliMCParticleContainer(containerBranch.c_str());
    }
    else {
      cont = new AliTrackContainer(containerBranch.c_str());
    }
    AdoptParticleContainer(dynamic_cast<AliParticleContainer *>(cont));
  }
  cont->SetName(containerName.c_str());

  return cont;
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::AddContainersToComponent(AliEmcalCorrectionComponent * component, InputObject_t inputObjectType)
{
  std::string inputObjectName = GetInputFieldNameFromInputObjectType(inputObjectType);
  // Need to be of the form "clusterContainersNames"
  inputObjectName = inputObjectName + "Names";

  //std::cout << "inputObjectName: " << inputObjectName << std::endl;
  std::vector <std::string> inputObjects;
  // Not required, because not all components need Clusters or Tracks
  AliEmcalCorrectionComponent::GetProperty(inputObjectName.c_str(), inputObjects, fUserConfiguration, fDefaultConfiguration, false, component->GetName());

  //std::cout << "inputObjects.size(): " << inputObjects.size() << std::endl;

  // If it is not found, then there will be nothing to iterate over, so we don't need to check the return value
  for (auto const & str : inputObjects)
  {
    // TODO: Generalize to arrays for clusters and tracks...
    // NOTE: The contianers operate differently than the cells. The containers should be executed during initialization while the cells should be executed during run time!
    if (inputObjectType == kCluster) {
      AliEmcalContainer * cont = GetClusterContainer(str.c_str());
      AliDebugStream(2) << "Adding cluster container " << str << " of array " << cont->GetArrayName() << " to component " << component->GetName() << std::endl;
      component->SetClusterContainer(GetClusterContainer(str.c_str()));
    }
    else if (inputObjectType == kTrack) {
      AliEmcalContainer * cont = GetParticleContainer(str.c_str());
      AliDebugStream(2) << "Adding particle container " << str << " of array " << cont->GetArrayName() << " to component " << component->GetName() << std::endl;
      component->SetParticleContainer(GetParticleContainer(str.c_str()));
    }
    else if (inputObjectType == kCaloCells) {
      // NOTE: This operates different than the others. This should be executed during run time rather than during initialization!
      if (inputObjects.size() > 1) {
        AliFatal(TString::Format("Component %s requested more than one cell branch, but this is not supported! Check the configuration!", component->GetName()));
      }
      // If we've made it here, this must be at least one entry
      AliDebugStream(2) << "Adding calo cells " << GetCellContainer(str)->GetName() << " of branch name " << GetCellContainer(str)->GetBranchName() << "to component " << component->GetName() << std::endl;
      component->SetCaloCells(GetCellContainer(str)->GetCells());
      AliDebugStream(3) << "component GetNumberOfCells: " << component->GetCaloCells()->GetNumberOfCells() << std::endl;
    }
  }
}

/**
 *
 *
 */
AliEmcalCorrectionCellContainer * AliEmcalCorrectionTask::GetCellContainer(const std::string & cellsContainerName) const
{
  for (auto cellContainer : fCellCollArray)
  {
    if (cellContainer->GetName() == cellsContainerName) {
      return cellContainer;
    }
  }

  return 0;
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::SetCellsObjectInCellContainerBasedOnProperties(AliEmcalCorrectionCellContainer * cellContainer)
{
  AliDebugStream(2) << "Retrieving cells object " << cellContainer->GetName() << std::endl;
  // Check for embedding and return object
  AliVEvent * event = GetEvent(InputEvent(), cellContainer->GetIsEmbedding());

  cellContainer->SetCells(dynamic_cast<AliVCaloCells *>(event->FindListObject(cellContainer->GetBranchName().c_str())));
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::ExecOnceComponents()
{
  // Run the initialization for all derived classes.
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  for (auto component : fCorrectionComponents)
  {
    // Setup geomertry
    component->SetEMCALGeometry(fGeom);

    // Add the requested cells to the component
    AliDebugStream(3) << "Adding CaloCells" << std::endl;
    AddContainersToComponent(component, kCaloCells);
    AliDebugStream(3) << "Added CaloCells" << std::endl;

    // Component ExecOnce()
    component->ExecOnce();
  }
}



/**
 * Retrieve objects from event.
 * @return
 */
Bool_t AliEmcalCorrectionTask::RetrieveEventObjects()
{
  fVertex[0] = 0;
  fVertex[1] = 0;
  fVertex[2] = 0;
  fNVertCont = 0;

  const AliVVertex *vert = InputEvent()->GetPrimaryVertex();
  if (vert) {
    vert->GetXYZ(fVertex);
    fNVertCont = vert->GetNContributors();
  }

  fBeamType = GetBeamType();

  if (fBeamType == kAA || fBeamType == kpA ) {
    if (fUseNewCentralityEstimation) {
      AliMultSelection *MultSelection = static_cast<AliMultSelection*>(InputEvent()->FindListObject("MultSelection"));
      if (MultSelection) {
        fCent = MultSelection->GetMultiplicityPercentile(fCentEst.Data());
      }
      else {
        AliWarning("Could not retrieve centrality information! Assuming 99");
      }
    }
    else { // old centrality estimation < 2015
      AliCentrality *aliCent = InputEvent()->GetCentrality();
      if (aliCent) {
        fCent = aliCent->GetCentralityPercentile(fCentEst.Data());
      }
      else {
        AliWarning("Could not retrieve centrality information! Assuming 99");
      }
    }

    if (fNcentBins==4) {
      if      (fCent >=  0 && fCent <   10) fCentBin = 0;
      else if (fCent >= 10 && fCent <   30) fCentBin = 1;
      else if (fCent >= 30 && fCent <   50) fCentBin = 2;
      else if (fCent >= 50 && fCent <= 100) fCentBin = 3;
      else {
        AliWarning(Form("Negative centrality: %f. Assuming 99", fCent));
        fCentBin = fNcentBins-1;
      }
    }
    else if (fNcentBins==5) {  // for PbPb 2015
      if      (fCent >=  0 && fCent <   10) fCentBin = 0;
      else if (fCent >= 10 && fCent <   30) fCentBin = 1;
      else if (fCent >= 30 && fCent <   50) fCentBin = 2;
      else if (fCent >= 50 && fCent <= 90) fCentBin = 3;
      else if (fCent > 90) {
        fCent = 99;
        fCentBin = 4;
      }
      else {
        AliWarning(Form("Negative centrality: %f. Assuming 99", fCent));
        fCentBin = fNcentBins-1;
      }
    }
    else {
      Double_t centWidth = (fMaxCent-fMinCent)/(Double_t)fNcentBins;
      if(centWidth>0.) {
        fCentBin = TMath::FloorNint(fCent/centWidth);
      }
      else {
        fCentBin = 0;
      }
      if (fCentBin>=fNcentBins) {
        AliWarning(Form("fCentBin too large: cent = %f fCentBin = %d. Assuming 99", fCent, fCentBin));
        fCentBin = fNcentBins-1;
      }
    }
  }
  else {
    fCent = 99;
    fCentBin = 0;
  }

  AliEmcalContainer* cont = 0;

  TIter nextPartColl(&fParticleCollArray);
  while ((cont = static_cast<AliEmcalContainer*>(nextPartColl()))) cont->NextEvent();

  TIter nextClusColl(&fClusterCollArray);
  while ((cont = static_cast<AliEmcalContainer*>(nextClusColl()))) cont->NextEvent();

  return kTRUE;
}


/**
 * Perform steps needed to initialize the analysis.
 * This function relies on the presence of an input
 * event (ESD or AOD event). Consequently it is called
 * internally by UserExec for the first event.
 *
 * This function connects all containers attached to
 * this task to the corresponding arrays in the
 * input event. Furthermore it initializes the geometry.
 */
void AliEmcalCorrectionTask::ExecOnce()
{
  if (!InputEvent()) {
    AliError("Could not retrieve event! Returning!");
    return;
  }

  if (fNeedEmcalGeom) {
    fGeom = AliEMCALGeometry::GetInstanceFromRunNumber(InputEvent()->GetRunNumber());
    if (!fGeom) {
      AliFatal("Can not get EMCal geometry instance. If you do not need the EMCal geometry, disable it by setting task->SetNeedEmcalGeometry(kFALSE).");
      return;
    }
  }

  // Load all requested track branches - each container knows name already
  for (Int_t i =0; i<fParticleCollArray.GetEntriesFast(); i++) {
    AliParticleContainer *cont = static_cast<AliParticleContainer*>(fParticleCollArray.At(i));
    CheckForContainerArray(cont, kTrack);
    cont->SetArray(InputEvent());
  }

  // Load all requested cluster branches - each container knows name already
  for (Int_t i =0; i<fClusterCollArray.GetEntriesFast(); i++) {
    AliClusterContainer *cont = static_cast<AliClusterContainer*>(fClusterCollArray.At(i));
    CheckForContainerArray(cont, kCluster);
    cont->SetArray(InputEvent());
  }

  // Determine the proper pointer for each cell object and save them to the cell contianer
  // At this point, they should all be created
  for (auto cellObj : fCellCollArray)
  {
    SetCellsObjectInCellContainerBasedOnProperties(cellObj);
  }

  fEventInitialized = kTRUE;

  ExecOnceComponents();
}

/**
 *
 *
 */
AliVEvent * AliEmcalCorrectionTask::GetEvent(AliVEvent * inputEvent, bool isEmbedding)
{
  AliVEvent * event = 0;
  if (isEmbedding) {
    // TODO: Enable embedded when that branch is committed!
    /*const AliAnalysisTaskEmcalEmbeddingHelper* embedding = AliAnalysisTaskEmcalEmbeddingHelper::GetInstance();
    if (!embedding) return 0;

    event = embedding->GetExternalEvent();*/
  }
  else {
    event = inputEvent;
  }

  return event;
}

/**
 *
 *
 */
void AliEmcalCorrectionTask::CheckForContainerArray(AliEmcalContainer * cont, InputObject_t objectType)
{
  // TODO: Enable embedded when that branch is committed!
  //AliVEvent * event = GetEvent(InputEvent(), cont->GetIsEmbedding());
  AliVEvent * event = GetEvent(InputEvent());

  TClonesArray *  array = dynamic_cast<TClonesArray *>(event->FindListObject(cont->GetArrayName()));
  if (!array) {
    AliWarning(TString::Format("Container %s requested branch %s, but it does not exist! Creating it for you! Please check that this the proper action!", cont->GetName(), cont->GetArrayName().Data()));
    array = new TClonesArray(DetermineUseDefaultName(objectType, fIsEsd, true).c_str());
    array->SetName(cont->GetArrayName());
    event->AddObject(array);
  }
}

/**
 * Create new container for MC particles and attach it to the task. The name
 * provided to this function must match the name of the array attached
 * to the new container inside the input event.
 * @param[in] n Name of the container and the array the container points to
 * @return Pointer to the new container for MC particles
 */
AliMCParticleContainer* AliEmcalCorrectionTask::AddMCParticleContainer(const char *n)
{
  if (TString(n).IsNull()) return 0;

  AliMCParticleContainer* cont = new AliMCParticleContainer(n);

  fParticleCollArray.Add(cont);

  return cont;
}

/**
 * Create new track container and attach it to the task. The name
 * provided to this function must match the name of the array attached
 * to the new container inside the input event.
 * @param[in] n Name of the container and the array the container points to
 * @return Pointer to the new track container
 */
AliTrackContainer* AliEmcalCorrectionTask::AddTrackContainer(const char *n)
{
  if (TString(n).IsNull()) return 0;

  AliTrackContainer* cont = new AliTrackContainer(n);

  fParticleCollArray.Add(cont);

  return cont;
}

/**
 * Create new particle container and attach it to the task. The name
 * provided to this function must match the name of the array attached
 * to the new container inside the input event.
 * @param[in] n Name of the container and the array the container points to
 * @return Pointer to the new particle container
 */
AliParticleContainer* AliEmcalCorrectionTask::AddParticleContainer(const char *n) 
{
  if (TString(n).IsNull()) return 0;

  AliParticleContainer* cont = new AliParticleContainer(n);

  fParticleCollArray.Add(cont);

  return cont;
}

/**
 * Create new cluster container and attach it to the task. The name
 * provided to this function must match the name of the array attached
 * to the new container inside the input event.
 * @param[in] n Name of the container and the array the container points to
 * @return Pointer to the new cluster container
 */
AliClusterContainer* AliEmcalCorrectionTask::AddClusterContainer(const char *n) 
{
  if (TString(n).IsNull()) return 0;

  AliClusterContainer* cont = new AliClusterContainer(n);

  fClusterCollArray.Add(cont);

  return cont;
}

/**
 * Get \f$ i^{th} \f$ particle container attached to this task
 * @param[in] i Index of the particle container
 * @return Particle container found for the given index (NULL if no particle container exists for that index)
 */
AliParticleContainer* AliEmcalCorrectionTask::GetParticleContainer(Int_t i) const 
{
  if (i<0 || i>fParticleCollArray.GetEntriesFast()) return 0;
  AliParticleContainer *cont = static_cast<AliParticleContainer*>(fParticleCollArray.At(i));
  return cont;
}

/**
 * Get \f$ i^{th} \f$ cluster container attached to this task
 * @param[in] i Index of the cluster container
 * @return Cluster container found for the given index (NULL if no cluster container exists for that index)
 */
AliClusterContainer* AliEmcalCorrectionTask::GetClusterContainer(Int_t i) const 
{
  if (i<0 || i>fClusterCollArray.GetEntriesFast()) return 0;
  AliClusterContainer *cont = static_cast<AliClusterContainer*>(fClusterCollArray.At(i));
  return cont;
}

/**
 * Find particle container attached to this task according to its name
 * @param[in] name Name of the particle container
 * @return Particle container found under the given name
 */
AliParticleContainer* AliEmcalCorrectionTask::GetParticleContainer(const char *name) const 
{
  AliParticleContainer *cont = static_cast<AliParticleContainer*>(fParticleCollArray.FindObject(name));
  return cont;
}

/**
 * Find cluster container attached to this task according to its name
 * @param[in] name Name of the cluster container
 * @return Cluster container found under the given name
 */
AliClusterContainer* AliEmcalCorrectionTask::GetClusterContainer(const char *name) const 
{
  AliClusterContainer *cont = static_cast<AliClusterContainer*>(fClusterCollArray.FindObject(name));
  return cont;
}


/**
 *
 *
 */
void AliEmcalCorrectionTask::UserExec(Option_t *option)
{
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));

  // Initialize the event if not intialized
  if (!fEventInitialized)
    ExecOnce();

  // Only continue if we are initialized successfully
  if (!fEventInitialized)
    return;

  // Get the objects for each event
  if (!RetrieveEventObjects())
    return;

  // TODO: Consider adding IsEventSelected()??

  // Call run for each correction
  if (!Run())
    return;
}

/**
 *
 *
 */
Bool_t AliEmcalCorrectionTask::Run()
{
  // Run the initialization for all derived classes.
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  for (auto component : fCorrectionComponents)
  {
    component->SetEvent(InputEvent());
    component->SetMCEvent(MCEvent());
    component->SetCentralityBin(fCentBin);
    component->SetCentrality(fCent);

    component->Run();
  }

  PostData(1, fOutput);

  // Need something more sophisticated here
  return kTRUE;
}

/**
 *
 *
 */
Bool_t AliEmcalCorrectionTask::UserNotify()
{
  // Run the initialization for all derived classes.
  AliDebug(3, Form("%s", __PRETTY_FUNCTION__));
  for (auto component : fCorrectionComponents)
  {
    component->UserNotify();
  }

  // Need something more sophisticated here
  return kTRUE;
}

/**
 *
 *
 */
AliEmcalCorrectionTask::~AliEmcalCorrectionTask()
{
  // Destructor
}

/**
 * Get beam type : pp-AA-pA
 * ESDs have it directly, AODs get it from hardcoded run number ranges
 * @return Beam type of the run.
 */
AliEmcalCorrectionTask::BeamType AliEmcalCorrectionTask::GetBeamType()
{
  if (fForceBeamType != kNA)
    return fForceBeamType;

  AliESDEvent *esd = dynamic_cast<AliESDEvent*>(InputEvent());
  if (esd) {
    const AliESDRun *run = esd->GetESDRun();
    TString beamType = run->GetBeamType();
    if (beamType == "p-p")
      return kpp;
    else if (beamType == "A-A")
      return kAA;
    else if (beamType == "p-A")
      return kpA;
    else
      return kNA;
  } else {
    Int_t runNumber = InputEvent()->GetRunNumber();
    if ((runNumber >= 136851 && runNumber <= 139517) ||  // LHC10h
        (runNumber >= 166529 && runNumber <= 170593)) {  // LHC11h
      return kAA;
    } else if ((runNumber>=188365 && runNumber <= 188366) ||   // LHC12g
        (runNumber >= 195344 && runNumber <= 196608)) { // LHC13b-f
      return kpA;
    } else {
      return kpp;
    }
  }  
}