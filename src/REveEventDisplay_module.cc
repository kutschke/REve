//Author: S Middleton
//Date: 2021
//Purpose: REveEventDisplay Driving module

#include "TRACE/trace.h"
//Art:
#include "art/Framework/Core/EDAnalyzer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Principal/Handle.h"
#include "art/Framework/Principal/Run.h"
#include "canvas/Utilities/InputTag.h"
#include "art_root_io/TFileService.h"
#include "art_root_io/TFileDirectory.h"
#include "canvas/Persistency/Common/TriggerResults.h"
#include "art/Framework/Services/System/TriggerNamesService.h"
#include "cetlib_except/exception.h"
#include "fhiclcpp/ParameterSet.h"
#include "fhiclcpp/ParameterSetRegistry.h"

#include "artdaq-core/Data/ContainerFragment.hh"
#include "artdaq-core/Data/Fragment.hh"
//#include "artdaq/DAQdata/Globals.hh"

#include "cetlib_except/exception.h"

//ROOT:
//#include "art_root_io/TFileService.h" 
#include <TApplication.h>
#include <TSystem.h>
#include <TList.h>
#include <TObjArray.h>
#include <Rtypes.h>
#include <TFile.h>
#include <TTree.h>

//EVE-7
#include <ROOT/RWebWindow.hxx>
#include <ROOT/RWebWindowsManager.hxx>
#include <ROOT/REveManager.hxx>

#include <condition_variable>
#include <iostream>
#include <mutex>
#include <thread>

//Offline:
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wtype-limits" 
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic pop

//EveMu2e
#include "REve/inc/REveMainWindow.hh"
#include "REve/inc/EventDisplayManager.hh"
#include "REve/inc/CollectionFiller.hh"
#include "REve/inc/DataCollections.hh"
#include "REve/inc/REveMu2eGUI.hh"
#include "REve/inc/REveCaloCluster.hh"
#include "REve/inc/REveComboHit.hh"
#include "REve/inc/REveDataProduct.hh"

#include "Offline/RecoDataProducts/inc/CaloCluster.hh"
#include "Offline/RecoDataProducts/inc/ComboHit.hh"
#include "Offline/GeometryService/inc/GeomHandle.hh"
#include "Offline/CalorimeterGeom/inc/CaloGeomUtil.hh"
#include "Offline/CalorimeterGeom/inc/Calorimeter.hh"
#include "Offline/GeometryService/inc/DetectorSystem.hh"
#include "Offline/Mu2eInterfaces/inc/Detector.hh"


using namespace std;
using namespace mu2e;
namespace mu2e
{
    class XThreadTimer : public TTimer {
        std::function<void()> foo_;
        public:
            XThreadTimer(std::function<void()> f) : foo_(f)
            {
                SetTime(0);
                R__LOCKGUARD2(gSystemMutex);
                gSystem->AddTimer(this);
            }
            Bool_t Notify() override
            {
                foo_();
                gSystem->RemoveTimer(this);
                return kTRUE;
            }
    };

    class REveEventDisplay : public art::EDAnalyzer {
      public:
        struct Config{
          using Name=fhicl::Name;
          using Comment=fhicl::Comment;
          fhicl::Atom<int> diagLevel{Name("diagLevel"), Comment("for info"),0};
          fhicl::Atom<bool> showCRV{Name("showCRV"), Comment("set false if you just want to see DS"),false};
          fhicl::Atom<bool> showPS{Name("showPS"), Comment("set false if you just want to see inside DS"),false};     
          fhicl::Atom<bool> showTS{Name("showTS"), Comment("set false if you just want to see inside DS"),false}; 
          fhicl::Atom<bool> showDS{Name("showDS"), Comment("set false if you just want to see inside DS"),false};    
          fhicl::Atom<bool> show2D{Name("show2D"), Comment(""),true};   
          fhicl::Atom<bool> caloVST{Name("caloVST"), Comment(""),false};  
          fhicl::Atom<bool> specifyTag{Name("specifyTag"), Comment("to only select events of selected input tag"),false};   
          fhicl::Table<CollectionFiller::Config> filler{Name("filler"),Comment("fill collections")};
          fhicl::Sequence<int>particles{Name("particles"),Comment("PDGcodes to plot")};
          fhicl::Atom<std::string>gdmlname{Name("gdmlname"),Comment("gdmlname")};
          fhicl::Atom<bool> strawdisplay{Name("strawdisplay"), Comment(""),true};
          fhicl::Atom<bool> seqMode{Name("seqMode"), Comment("turn off for go to any event functionality"),true};
        };

        typedef art::EDAnalyzer::Table<Config> Parameters;
        explicit REveEventDisplay(const Parameters& conf);
        virtual ~REveEventDisplay();
        virtual void beginJob() override;
        virtual void beginRun(const art::Run& run) override;
        virtual void analyze(const art::Event& e);
        virtual void endJob() override;
        void signalAppStart();
      private:

        art::ServiceHandle<art::TFileService> tfs;
        Config _conf;
        int  diagLevel_;
        bool showCRV_;
        bool showPS_;
        bool showTS_;   
        bool showDS_;
        bool show2D_;
        bool caloVST_;
        bool specifyTag_ = false;
        
        void setup_eve();
        void run_application();
        void process_single_event();
        void printOpts();
        template <class T, class S> void FillAnyCollection(const art::Event& evt, std::vector<std::shared_ptr<REveDataProduct>>& list, std::tuple<std::vector<std::string>, std::vector<S>>& tuple);

        // Application control
        TApplication application_{"none", nullptr, nullptr};
        std::thread appThread_{};

        // Display control
        art::EventID displayedEventID_{};
        REX::REveManager* eveMng_{nullptr};
        EventDisplayManager* eventMgr_{nullptr};

        // Control between the main thread and event-display thread
        std::condition_variable cv_{};
        std::mutex m_{};

        TDirectory*   directory_ = nullptr;   
        CollectionFiller filler_;
        REveMainWindow *frame_;
        DataCollections data;
        bool firstLoop_ = true; 
        std::vector<int> particles_;
        std::string gdmlname_;
        bool strawdisplay_; 
        
        // Setup Custom GUI
        REveMu2eGUI *fGui{nullptr};
        double eventid_;   
        double runid_;   
        double subrunid_;
        bool seqMode_;
        int eventn;
        int runn;
        
        std::vector<std::shared_ptr<REveDataProduct>> listoflists;
        
    };


  REveEventDisplay::REveEventDisplay(const Parameters& conf)  :
    art::EDAnalyzer(conf),
    diagLevel_(conf().diagLevel()),
    showCRV_(conf().showCRV()),
    showPS_(conf().showPS()),
    showTS_(conf().showTS()),
    showDS_(conf().showDS()),
    show2D_(conf().show2D()),
    caloVST_(conf().caloVST()),
    specifyTag_(conf().specifyTag()),
    filler_(conf().filler()),
    particles_(conf().particles()),
    gdmlname_(conf().gdmlname()),
    strawdisplay_(conf().strawdisplay()),
    seqMode_(conf().seqMode())
    {
       if(!seqMode_){  
        // Take in Run, Event number
          std::cout<<" Event Number : "<<std::endl;
          cin>>eventn;
          std::cout<<" Run Number : "<<std::endl;
          cin>>runn;
      }
    }

  REveEventDisplay::~REveEventDisplay() {}

  void REveEventDisplay::signalAppStart()
  {
      std::unique_lock lock{m_};
      cv_.notify_all();
  }

  void REveEventDisplay::beginJob(){
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : beginJob()] -- starting ..."<<std::endl;
      {
      std::unique_lock lock{m_};

      appThread_ = std::thread{[this] { run_application(); }};

      // Wait for app init to finish ... this will process pending timer events.
      XThreadTimer sut([this]{ signalAppStart(); });
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : beginJob()] -- starting wait on app start"<<std::endl;
      cv_.wait(lock);
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : beginJob()] -- app start signal received, starting eve init"<<std::endl;
      XThreadTimer suet([this]{ setup_eve(); });
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : beginJob()] -- starting wait on eve setup"<<std::endl;
      cv_.wait(lock);
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : beginJob()] -- eve setup apparently complete"<<std::endl;
      }
  }


  void REveEventDisplay::beginRun(const art::Run&){
  }

  void REveEventDisplay::printOpts(){
    std::cout<<"*********** REve Mu2e **************"
    <<" User Options: "
    <<" addHits : "<<filler_.addHits_
    <<" addTimeClusters : "<<filler_.addTimeClusters_
    <<" addCRVpulses : "<<filler_.addCrvHits_
    <<" addClusters : "<<filler_.addClusters_
    <<" addTracks : "<<filler_.addKalSeeds_
    <<" addCosmicTrackSeeds : "<<filler_.addCosmicTrackSeeds_
    <<" add CRV : "<<filler_.addCrvHits_<<std::endl;
  }
  
  
  template <class T, class S> void REveEventDisplay::FillAnyCollection(const art::Event& evt, std::vector<std::shared_ptr<REveDataProduct>>& list, std::tuple<std::vector<std::string>, std::vector<S>>& tuple){
      // get all instances of products of type T
      std::vector<art::Handle<T>> vah = evt.getMany<T>();
      std::string name;
      std::vector<std::string> alabel;
      std::vector<S> alist;
      // loop over the list of instances of products of this type
      for (auto const& ah : vah) {
        const art::Provenance* prov = ah.provenance();
        
        std::string fcn = prov->friendlyClassName();
        std::string modn = prov->moduleLabel();
        std::string instn = prov->processName();
        alist.push_back(ah.product());
        std::string name = fcn + "_" + prov->moduleLabel() + "_" + instn;
        alabel.push_back(name);
        if(diagLevel_ == 1){
          std::cout<<"extracting name =  "<<fcn<<" "<<modn<<" "<<instn<<std::endl; 
          std::cout<<"with type =  "<<typeid(prov).name()<<std::endl;
        } 
      }
      tuple = std::make_tuple(alabel,alist);
      
    }
    
  void REveEventDisplay::analyze(art::Event const& event){
      //remove previous event objects
      data.Reset();
      
      // Update state relevant for displaying new event.
      displayedEventID_ = event.id();
      eventid_ = event.id().event(); 
      runid_ = event.run();
      subrunid_ = event.subRun();

      std::vector<std::shared_ptr<REveDataProduct>> _chits;
     
      if((seqMode_) or ( runid_ == runn and eventid_ == eventn)){
        // Hand off control to display thread
        std::unique_lock lock{m_};
        if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : analyze()] -- Fill collections "<<std::endl;
        
        // fill the collection lists
        if(filler_.addClusters_) {
          if(specifyTag_) filler_.FillRecoCollections(event, data, CaloClusters);
          else { FillAnyCollection<CaloClusterCollection, const CaloClusterCollection*>(event, _chits, data.calocluster_tuple);}
        }
        
        if(filler_.addHits_) {
          if(specifyTag_) { filler_.FillRecoCollections(event, data, ComboHits); }
          else { FillAnyCollection<ComboHitCollection, const ComboHitCollection*>(event, _chits, data.combohit_tuple ); }
        }
        
        if(filler_.addKalSeeds_) {
          if(specifyTag_) { filler_.FillRecoCollections(event, data, KalSeeds); }
          else { FillAnyCollection<KalSeedCollection, const KalSeedCollection*>(event, _chits, data.track_tuple ); }
        }
        
        if(filler_.addMCTraj_) { 
          if(specifyTag_) { filler_.FillMCCollections(event, data, MCTrajectories); }
          else { FillAnyCollection<MCTrajectoryCollection, const MCTrajectoryCollection*>(event, _chits, data.mctrack_tuple ); }
        }
        
        if(filler_.addTimeClusters_) {
          if(specifyTag_) { filler_.FillRecoCollections(event, data, TimeClusters);}
          else { FillAnyCollection<TimeClusterCollection, const TimeClusterCollection*>(event, _chits, data.timecluster_tuple );}
        }
        
        if(filler_.addCrvHits_) {
          if(specifyTag_) { filler_.FillRecoCollections(event, data, CRVRecoPulses); }
          else { FillAnyCollection<CrvRecoPulseCollection, const CrvRecoPulseCollection*>(event, _chits, data.crvpulse_tuple );}
        }

        if(filler_.addTrkHits_) filler_.FillRecoCollections(event, data, TrkHits); 
        if(filler_.addCosmicTrackSeeds_)  filler_.FillRecoCollections(event, data, CosmicTrackSeeds);
        
        if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : analyze()] -- Event processing started "<<std::endl;
        XThreadTimer proc_timer([this]{ process_single_event(); });
        if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : analyze()] -- transferring to TApplication thread "<<std::endl;
        cv_.wait(lock);
        if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : analyze()] -- TApplication thread returning control "<<std::endl;
        if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : analyze()] Ended Event "<<std::endl; 
        seqMode_ = true;
      }
  }

    void REveEventDisplay::endJob()
    {
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : EndJob] Start "<<std::endl;
      application_.Terminate(0);

      if (appThread_.joinable()) {
        appThread_.join();
      }
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : EndJob] End "<<std::endl;
    }



  // Functions invoked by the threads explicitly created by this module
  void REveEventDisplay::run_application()
  {
      // Without this the startup timer might not get invoked.
      gSystem->ProcessEvents();
      application_.Run(true);
  }


  void REveEventDisplay::setup_eve()
  {
      REX::RWebWindowsManager::AssignMainThrd();
      eveMng_ = REX::REveManager::Create();

      // InitGuiInfo()
      fGui = new REveMu2eGUI();
      fGui->SetName("Mu2eGUI");
      
      // call manager
      eventMgr_ = new EventDisplayManager{eveMng_, cv_, m_, fGui};
      
      // access the world
      auto world = eveMng_->GetWorld();
      
      assert(world);
      world->AddElement(eventMgr_);
      
      
      frame_ = new REveMainWindow();
      GeomOptions geomOpts(showCRV_,showPS_, showTS_, showDS_, show2D_, caloVST_ );
      frame_->makeGeometryScene(eveMng_, geomOpts, gdmlname_);
      
      //add path to the custom GUI code here, this overrides ROOT GUI
      eveMng_->AddLocation("mydir/", "REve/CustomGUI");
      eveMng_->SetDefaultHtmlPage("file:mydir/eventDisplay.html");
   
      // InitGuiInfo() cont'd
      world->AddElement(fGui);
      world->AddCommand("QuitRoot",  "sap-icon://log",  eventMgr_, "QuitRoot()");
      world->AddCommand("NextEvent", "sap-icon://step", eventMgr_, "NextEvent()");
      //world->AddCommand("PrintEventInfo", "sap-icon://step", fGui, "PrintEventInfo()");
      std::unique_lock lock{m_};
      cv_.notify_all();
 
  }

  // Actually interesting function responsible for drawing the current event
  void REveEventDisplay::process_single_event()
    {    
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : process_single_event] Start "<<std::endl;
      eveMng_->DisableRedraw();
      eveMng_->GetWorld()->BeginAcceptingChanges();
      eveMng_->GetScenes()->AcceptChanges(true);

      fGui->feventid = eventid_;
      fGui->frunid = subrunid_;
      fGui->frunid = runid_;
      fGui->StampObjProps();

      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : process_single_event] -- extract event scene "<<std::endl;
      REX::REveElement* scene = eveMng_->GetEventScene();

      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : process_single_event] -- calls to data interface "<<std::endl;
      DrawOptions drawOpts(filler_.addCosmicTrackSeeds_, filler_.addKalSeeds_, filler_.addClusters_, filler_.addHits_,  filler_.addCrvHits_, filler_.addTimeClusters_, filler_.addTrkHits_, filler_.addMCTraj_);
      frame_->showEvents(eveMng_, scene, firstLoop_, data, drawOpts, particles_, strawdisplay_);

      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : process_single_event] -- cluster added to scene "<<std::endl;
      firstLoop_ = false;
      eveMng_->GetScenes()->AcceptChanges(false);
      eveMng_->GetWorld()->EndAcceptingChanges();
      eveMng_->EnableRedraw();
      
      if(diagLevel_ == 1) std::cout<<"[REveEventDisplay : process_single_event] End "<<std::endl;
    }
  }
  using mu2e::REveEventDisplay;
  DEFINE_ART_MODULE(REveEventDisplay)
