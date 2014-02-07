/*
 *  cWorld.cc
 *  Avida
 *
 *  Created by David on 10/18/05.
 *  Copyright 1999-2011 Michigan State University. All rights reserved.
 *
 *
 *  This file is part of Avida.
 *
 *  Avida is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation, either version 3 of the License, or (at your option) any later version.
 *
 *  Avida is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public License along with Avida.
 *  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "cWorld.h"

#include "avida/data/Manager.h"
#include "avida/environment/Manager.h"
#include "avida/output/Manager.h"
#include "avida/systematics/Arbiter.h"
#include "avida/systematics/Manager.h"

#include "avida/private/systematics/GenotypeArbiter.h"

#include "cEnvironment.h"
#include "cEventList.h"
#include "cHardwareManager.h"
#include "cPopulation.h"
#include "cStats.h"
#include "cTestCPU.h"
#include "cUserFeedback.h"

#include <cassert>


cWorld::cWorld(cAvidaConfig* cfg, const cString& wd)
  : m_working_dir(wd), m_analyze(NULL), m_conf(cfg)
  , m_env(NULL), m_event_list(NULL), m_hw_mgr(NULL), m_pop(NULL), m_stats(NULL), m_data_mgr(NULL)
{
}

cWorld* cWorld::Initialize(cAvidaConfig* cfg, const cString& working_dir, Universe* new_world, cUserFeedback* feedback, const Apto::Map<Apto::String, Apto::String>* mappings)
{
  cWorld* world = new cWorld(cfg, working_dir);
  if (!world->setup(new_world, feedback, mappings)) {
    delete world;
    world = NULL;
  }
  return world;
}

cWorld::~cWorld()
{
  // m_actlib is not owned by cWorld, DO NOT DELETE
  
  // These must be deleted first
  delete m_analyze; m_analyze = NULL;
  
  // Forcefully clean up population before classification manager
  m_pop = Apto::SmartPtr<cPopulation, Apto::InternalRCObject>();
  
  delete m_env; m_env = NULL;
  delete m_event_list; m_event_list = NULL;
  delete m_hw_mgr; m_hw_mgr = NULL;

  // Delete Last
  delete m_conf; m_conf = NULL;
}


bool cWorld::setup(Universe* new_world, cUserFeedback* feedback, const Apto::Map<Apto::String, Apto::String>* defs)
{
  bool success = true;
  
  // Setup Random Number Generator
  m_rng.ResetSeed(m_conf->RANDOM_SEED.Get());
  
  // Initialize new API-based data structures here for now
  {
    // Data Manager
    m_data_mgr = Data::ManagerPtr(new Data::Manager);
    m_data_mgr->AttachTo(new_world);
    
    // Environment
    Environment::ManagerPtr(new Environment::Manager)->AttachTo(new_world);
    
    // Output Manager
    Apto::String opath = Apto::FileSystem::GetAbsolutePath(Apto::String(m_conf->DATA_DIR.Get()), Apto::String(m_working_dir));
    Output::ManagerPtr(new Output::Manager(opath))->AttachTo(new_world);
  }
  

  m_env = new cEnvironment(this);
    
  
  // Initialize the default environment...
  // This must be after the HardwareManager in case REACTIONS that trigger instructions are used.
  if (!m_env->Load(m_conf->ENVIRONMENT_FILE.Get(), m_working_dir, *feedback, defs)) {
    success = false;
  }
  
  
  // Systematics
  Systematics::ManagerPtr systematics(new Systematics::Manager);
  systematics->AttachTo(new_world);
  systematics->RegisterRole("genotype", Systematics::ArbiterPtr(new Systematics::GenotypeArbiter(new_world, m_conf->THRESHOLD.Get(), m_conf->DISABLE_GENOTYPE_CLASSIFICATION.Get())));

  
  // Setup Stats Object
  m_stats = Apto::SmartPtr<cStats, Apto::InternalRCObject>(new cStats(this));

  
  // Initialize the hardware manager, loading all of the instruction sets
  m_hw_mgr = new cHardwareManager(this);
  if (!m_hw_mgr->LoadInstSets(feedback)) success = false;
  if (m_hw_mgr->GetNumInstSets() == 0) {
    if (feedback) {
      feedback->Error("no instruction sets defined");
    }
    success = false;
  }
  
  // If there were errors loading at this point, it is perilous to try to go further (pop depends on an instruction set)
  if (!success) return success;
  
  

  m_pop = Apto::SmartPtr<cPopulation, Apto::InternalRCObject>(new cPopulation(this));
  
  // Setup Event List
  m_event_list = new cEventList(this);
  if (!m_event_list->LoadEventFile(m_conf->EVENT_FILE.Get(), m_working_dir, *feedback, defs)) {
    if (feedback) feedback->Error("unable to load event file");
    success = false;
  }
  
  return success;
}

Data::ProviderPtr cWorld::GetStatsProvider(Universe*) { return m_stats; }
Data::ArgumentedProviderPtr cWorld::GetPopulationProvider(Universe*) { return m_pop; }


void cWorld::GetEvents(cAvidaContext& ctx)
{  
  if (m_pop->GetSyncEvents() == true) {
    m_event_list->Sync();
    m_pop->SetSyncEvents(false);
  }
  m_event_list->Process(ctx);
}

