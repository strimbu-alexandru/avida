//
//  Callbacks.h
//  viewer-webed
//
//  Created by Matthew Rupp on 9/3/15.
//  Copyright (c) 2015 MSU. All rights reserved.


#ifndef viewer_webed_Callbacks_h
#define viewer_webed_Callbacks_h

#include "Driver.h"
#include <cstdlib>



/*
  It'd be nice to encapsulate this into a RunTime class.  Unfortunately, the actual
  RuntimeLoop function needs to be exposed to emscripten and cannot be a member method.
  So, we're going to do this the old fashioned way and play pass the driver while
  exposing some potentially dangerous retrieval functions
  on the driver object to delete it... through deleting its cWorld!
  
  The driver itself contains all information about a single experiment.  It is a little
  strange, though, since the driver moves the world but the driver also contains the world
  and the world, the driver.  It's best to think of Creating/Resetting/Deleting the Driver
  in this header as doing both at the same time to the world and the driver.  
  
  NEVER delete the driver object directly.  ALWAYS delete it through DeleteDriver().
  
  This is because the cWorld destructor actually deletes our driver.  They refer
  to one another, and this is the traditional behavior that I don't want to muck
  with.
*/

namespace Avida {
  namespace WebViewer {
  
  
    /*
      Just a quick class to store and retrieve settings for a world.
      The driver and InitializeToDefault method use this.  The cAvidaConfig
      pointer will be passed to a world object which will then delete it
      once the world has run its course.
    */
    class DriverConfig  
    {
      protected:
        cAvidaConfig* cfg;
        string working_dir;
      
      public:
        DriverConfig(cAvidaConfig* acfg, string& dir) : cfg(acfg), working_dir(dir) {}
        cAvidaConfig* GetConfig() { return cfg; }
        char* GetWorkingDir() { return working_dir.GetData(); };
    };
    
    
    /*
      Periodically, the RuntimeLoop will check for messages sent from
      different Worker threads.  These messages are sent to the
      driver for processing.  
    */
    void CheckMessages(Driver* driver)
    {
      EMStringPtr msg_buf = GetMessages();
      json msgs = nlohmann::json::parse( (char*) msg_buf);
      std::free( (void*) msg_buf);  //Cleanup JS allocation
      for (auto msg : msgs){
        driver->ProcessMessage(msg);
      }
    }
    
    
    /*
      Setup the driver using the default packaged settings.
      This will in turn create the world and by agreement set responsbility
      for deleting the driver to the cWorld object. 
    */
    extern "C"
    Driver* CreateDefaultDriver()
    {
      cAvidaConfig* cfg = new cAvidaConfig();
      cUserFeedback feedback = new cUserFeedback;
      
      Apto::Map<Apto::String, Apto::String> defs;
      Avida::Util::ProcessCmdLineArgs(argc, argv, cfg, defs);
      
      DriverConfig d_cfg(cfg,"/");
      Driver* driver = SetupDriver(d_cfg, &defs);
      D_(D_STATUS, "Avida is now configured with default settings.");
      return driver;
    }
    
    
    /*
      Setup the driver/world with a particular set of configuration options.
    */
    Driver* SetupDriver(DriverConfig& cfg, Apto::Map<Apto::String, Apto::String>* defs = nullptr)
    {
        //new_world, feedback, and the driver are all deleted when the cWorld object is deleted.
        World* new_world = new World;
        cUserFeedback* feedback = new cUserFeedback();
        cWorld* world = cWorld::Initialize(cfg.cfg, cfg.working_dir, new_world, feedback, defs);
        D_(D_STATUS, "The world is located at " << world);
        driver = new Driver(world, feedback);
        D_(D_STATUS, "The driver is located at " << &driver);
        return driver;
    }
    
    /*
      We'll let the world clean up the driver.
      DON'T directly delete the driver.  You'll
      crash my runtime.
    */
    void DeleteDriver(Driver*& driver)
    {
      delete driver->GetWorld();
      driver = nullptr;
    }
    
    
    /*
      Return a new driver (and delete the old) with new settings.
    */
    Driver* DriverReset(Driver* driver)
    {
      D_(D_FLOW | D_STATUS, "Resetting driver.");
      WebViewerMsg msg_reset = FeedbackMessage(Feedback:NOTIFICATION);
      msg_reset["description"] = "The Avida driver is resetting";
      DriverConfig d_cfg = driver->GetNextConfig();
      DeleteDriver(driver);
      return SetupDriver(d_cfg);
    }
  
  
    /*
      Clean up after ourselves before we return back to main to exit
      the program.
    */
    void AvidaExit(Driver* driver)
    {
      D_(D_FLOW | D_STATUS, "The Avida runtime is termiating.");
      WebViewerMsg msg_exit = FeedbackMessage(Feedback::FATAL);
      msg_exit["description"] = "Avida is exiting";
      PostMessage(msg_exit);
      DeleteDriver(driver);
    }
    
    
    
    /*
      I need to expose this function using C-style linkage
      so I can whitelist it for emterpretifying.  The RuntimeLoop
      handles the logic of transitioning between paused and running
      states; when to check for messages; and when to pause to
      let our browser do its own processing.  It also handles
      setting up our world/driver, resetting our world/driver, and
      preparing to exit the program.
    */
    extern "C"
    void RuntimeLoop()
    {
      Driver* driver = CreateDefaultDriver();
      
      D_(D_FLOW | D_STATUS, "Entering runtime loop");
      PostMessage(MSG_READY);
      
      /* Any call to CheckMessage could end up forcing us
        to delete the cWorld object (and the driver, feedback, etc.)
        objects.  Consequently, we always have to check to see
        if our driver still exists before polling its state.  If it
        is ever "finished" or a nullptr, we should break out of this
        function.
      */
      while(driver && !driver->IsFinished()){
      
        D_(D_STATUS, "Driver is active.");
        
        bool first_pass = true;
        while(driver && 
              driver->IsPaused() && 
              !driver->DoReset() ){
          if (first_pass){
            D_(D_STATUS, "Driver is paused.");
            first_pass = false;
          }
          emscripten_sleep(100);
          CheckMessages(driver);
        } //End paused loop
        
        first_pass = true;
        while( driver && 
               !( driver->IsFinished() && 
                  driver->IsPaused() &&
                  driver->DoReset()) ) {
          if (first_pass){
            D_(D_STATUS, "Driver is running.");
            first_pass = false;
          }
          driver->StepUpdate();
          emscripten_sleep(100);
          CheckMessages(driver);
        } // End step update loop
        
        //Reset requests require us to destroy the current world and driver
        //and initialize a new one before we proceed.
        if (driver && driver->DoReset()){
          driver = ResetDriver();
        }
      } // End driver available and not finished loop
      if (driver){
        AvidaExit(driver);
      } else {
        cerr << "We should never be here.  The driver was unavailable during runtime loop." << endl;
      }
    } // End RuntimeLoop
  } //End WebViewer namespace
} //End Avida namespace

#endif