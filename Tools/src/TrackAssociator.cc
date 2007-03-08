// -*- C++ -*-
//
// Package:    TrackAssociator
// Class:      TrackAssociator
// 
/*

 Description: <one line class summary>

 Implementation:
     <Notes on implementation>
*/
//
// Original Author:  Dmytro Kovalskyi
// Modified for ECAL+HCAL by:  Michal Szleper
//         Created:  Fri Apr 21 10:59:41 PDT 2006
// $Id: TrackAssociator.cc,v 1.6 2006/08/16 22:02:02 jribnik Exp $
//
//

#include "Calibration/Tools/interface/TrackAssociator.h"

// user include files
#include "FWCore/Framework/interface/Frameworkfwd.h"

#include "FWCore/Framework/interface/Event.h"
#include "FWCore/Framework/interface/EventSetup.h"
#include "FWCore/Framework/interface/MakerMacros.h"
#include "FWCore/Framework/interface/Handle.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/OrphanHandle.h"

#include "DataFormats/TrackReco/interface/Track.h"
#include "DataFormats/TrackReco/interface/TrackExtra.h"
#include "DataFormats/CaloTowers/interface/CaloTower.h"
#include "DataFormats/CaloTowers/interface/CaloTowerCollection.h"
#include "DataFormats/EgammaReco/interface/SuperCluster.h"
#include "DataFormats/DTRecHit/interface/DTRecHitCollection.h"
#include "DataFormats/EcalDetId/interface/EBDetId.h"
#include "DataFormats/DetId/interface/DetId.h"

// calorimeter info
#include "Geometry/Records/interface/IdealGeometryRecord.h"
#include "Geometry/CaloGeometry/interface/CaloGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloSubdetectorGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloCellGeometry.h"
#include "DataFormats/EcalDetId/interface/EcalSubdetector.h"
#include "DataFormats/HcalDetId/interface/HcalSubdetector.h"

#include "Geometry/Surface/interface/Cylinder.h"
#include "Geometry/Surface/interface/Plane.h"

#include "Geometry/CommonDetUnit/interface/GeomDetUnit.h"


#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"

#include "TrackPropagation/SteppingHelixPropagator/interface/SteppingHelixPropagator.h"
#include "Utilities/Timing/interface/TimingReport.h"
#include <stack>
#include <set>


#include "Calibration/Tools/interface/CaloDetIdAssociator.h"
#include "Calibration/Tools/interface/EcalDetIdAssociator.h"
#include "Calibration/Tools/interface/HcalDetIdAssociator.h"
#include "Calibration/Tools/interface/TimerStack.h"

#include "Geometry/CommonDetAlgo/interface/ErrorFrameTransformer.h"

//
// class declaration
//

using namespace reco;

TrackAssociator::TrackAssociator() 
{
   ivProp_ = 0;
   defProp_ = 0;
   debug_ = 0;
   caloTowerMap_ = 0;
   useDefaultPropagator_ = false;
}

TrackAssociator::~TrackAssociator()
{
   if (defProp_) delete defProp_;
}

//-----------------------------------------------------------------------------
void TrackAssociator::addDataLabels( const std::string className,
				     const std::string moduleLabel,
				     const std::string productInstanceLabel)
{
   if (className == "EBRecHitCollection")
     {
	EBRecHitCollectionLabels.clear();
	EBRecHitCollectionLabels.push_back(moduleLabel);
	EBRecHitCollectionLabels.push_back(productInstanceLabel);
     }
   if (className == "EERecHitCollection")
     {
	EERecHitCollectionLabels.clear();
	EERecHitCollectionLabels.push_back(moduleLabel);
	EERecHitCollectionLabels.push_back(productInstanceLabel);
     }
   if (className == "HBHERecHitCollection")
     {
	HBHERecHitCollectionLabels.clear();
	HBHERecHitCollectionLabels.push_back(moduleLabel);
	HBHERecHitCollectionLabels.push_back(productInstanceLabel);
     }
   if (className == "CaloTowerCollection")
     {
	CaloTowerCollectionLabels.clear();
	CaloTowerCollectionLabels.push_back(moduleLabel);
	CaloTowerCollectionLabels.push_back(productInstanceLabel);
     }
}


//-----------------------------------------------------------------------------
void TrackAssociator::setPropagator( Propagator* ptr)
{
   ivProp_ = ptr; 
   caloDetIdAssociator_.setPropagator(ivProp_);
   ecalDetIdAssociator_.setPropagator(ivProp_);
   hcalDetIdAssociator_.setPropagator(ivProp_);
}

//-----------------------------------------------------------------------------
void TrackAssociator::useDefaultPropagator()
{
   useDefaultPropagator_ = true;
}


//-----------------------------------------------------------------------------
void TrackAssociator::init( const edm::EventSetup& iSetup )
{
   // access the calorimeter geometry
   iSetup.get<IdealGeometryRecord>().get(theCaloGeometry_);
   if (!theCaloGeometry_.isValid()) 
     throw cms::Exception("FatalError") << "Unable to find IdealGeometryRecord in event!\n";
   
   if (useDefaultPropagator_ && ! defProp_ ) {
      // setup propagator
      edm::ESHandle<MagneticField> bField;
      iSetup.get<IdealMagneticFieldRecord>().get(bField);
      
      SteppingHelixPropagator* prop  = new SteppingHelixPropagator(&*bField,anyDirection);
      prop->setMaterialMode(false);
      prop->applyRadX0Correction(true);
      // prop->setDebug(true); // tmp
      defProp_ = prop;
      setPropagator(defProp_);
   }
   
	
}

//-----------------------------------------------------------------------------
TrackDetMatchInfo TrackAssociator::associate( const edm::Event& iEvent,
					      const edm::EventSetup& iSetup,
					      const FreeTrajectoryState& trackOrigin,
					      const AssociatorParameters& parameters )
{
   TrackDetMatchInfo info;
   using namespace edm;
   TimerStack timers;
   
   init( iSetup );
   
   FreeTrajectoryState currentPosition(trackOrigin);

   if (parameters.useEcal) fillEcal( iEvent, iSetup, info, currentPosition,parameters.idREcal, parameters.dREcal);
   if (parameters.useHcal) fillHcal( iEvent, iSetup, info, currentPosition,parameters.idRHcal,parameters.dRHcal);
   if (parameters.useCalo) fillCaloTowers( iEvent, iSetup, info, currentPosition,parameters.idRCalo,parameters.dRCalo);

   return info;
}

//-----------------------------------------------------------------------------
std::vector<EcalRecHit> TrackAssociator::associateEcal( const edm::Event& iEvent,
							const edm::EventSetup& iSetup,
							const FreeTrajectoryState& trackOrigin,
							const double dR )
{
   AssociatorParameters parameters;
   parameters.useHcal = false;
   parameters.dREcal = dR;
   TrackDetMatchInfo info( associate(iEvent, iSetup, trackOrigin, parameters ));
   if (dR>0) 
     return info.coneEcalRecHits;
   else
     return info.crossedEcalRecHits;
}

//-----------------------------------------------------------------------------
double TrackAssociator::getEcalEnergy( const edm::Event& iEvent,
				       const edm::EventSetup& iSetup,
				       const FreeTrajectoryState& trackOrigin,
				       const double dR )
{
   AssociatorParameters parameters;
   parameters.useHcal = false;
   parameters.dREcal = dR;
   TrackDetMatchInfo info = associate(iEvent, iSetup, trackOrigin, parameters );
   if(dR>0) 
     return info.ecalConeEnergyFromRecHits();
   else
     return info.ecalEnergyFromRecHits();
}

//-----------------------------------------------------------------------------
std::vector<CaloTower> TrackAssociator::associateHcal( const edm::Event& iEvent,
						       const edm::EventSetup& iSetup,
						       const FreeTrajectoryState& trackOrigin,
						       const double dR )
{
   AssociatorParameters parameters;
   parameters.useEcal = false;
   parameters.dRHcal = dR;
   TrackDetMatchInfo info( associate(iEvent, iSetup, trackOrigin, parameters ));
   if (dR>0) 
     return info.coneTowers;
   else
     return info.crossedTowers;
   
}

//-----------------------------------------------------------------------------
double TrackAssociator::getHcalEnergy( const edm::Event& iEvent,
				       const edm::EventSetup& iSetup,
				       const FreeTrajectoryState& trackOrigin,
				       const double dR )
{
   AssociatorParameters parameters;
   parameters.useEcal = false;
   parameters.dRHcal = dR;
   TrackDetMatchInfo info( associate(iEvent, iSetup, trackOrigin, parameters ));
   if (dR>0) 
     return info.hcalConeEnergyFromRecHits();
   else
     return info.hcalEnergyFromRecHits();
}

//-----------------------------------------------------------------------------
void TrackAssociator::fillEcal( const edm::Event& iEvent,
				const edm::EventSetup& iSetup,
				TrackDetMatchInfo& info,
				const FreeTrajectoryState& trajectoryPoint,
				const int idR,
				const double dR)
{
   TimerStack timers;
   timers.push("TrackAssociator::fillEcal");
   
   ecalDetIdAssociator_.setGeometry(&*theCaloGeometry_);
   
   timers.push("TrackAssociator::fillEcal::propagation");
   // ECAL points (EB+EE)
   std::vector<GlobalPoint> ecalPoints;
   ecalPoints.push_back(GlobalPoint(135.,0,310.));
   ecalPoints.push_back(GlobalPoint(150.,0,340.));
   ecalPoints.push_back(GlobalPoint(170.,0,370.));
   
   std::vector<GlobalPoint> ecalTrajectory = ecalDetIdAssociator_.getTrajectory(trajectoryPoint, ecalPoints);
//   if(ecalTrajectory.empty()) throw cms::Exception("FatalError") << "Failed to propagate a track to ECAL\n";

   if(ecalTrajectory.empty()) {
      LogTrace("TrackAssociator::fillEcal") << "Failed to propagate a track to ECAL; moving on\n";
      info.isGoodEcal = 0;
      return;
   }
   
   info.isGoodEcal = 1;

   info.trkGlobPosAtEcal = getPoint(ecalTrajectory[0]);

   // Find ECAL crystals
   timers.pop_and_push("TrackAssociator::fillEcal::access::EcalBarrel");
   edm::Handle<EBRecHitCollection> EBRecHits;
   edm::Handle<EERecHitCollection> EERecHits;
//   if (EBRecHitCollectionLabels.empty())
//     throw cms::Exception("FatalError") << "Module lable is not set for EBRecHitCollection.\n";
//   else
     iEvent.getByLabel (EBRecHitCollectionLabels[0], EBRecHitCollectionLabels[1], EBRecHits);
//   if (!EBRecHits.isValid()) throw cms::Exception("FatalError") << "Unable to find EBRecHitCollection in event!\n";
     if (EERecHitCollectionLabels[1]!=EBRecHitCollectionLabels[1]) iEvent.getByLabel (EERecHitCollectionLabels[0], EERecHitCollectionLabels[1], EERecHits);
//   if (!EERecHits.isValid()) throw cms::Exception("FatalError") << "Unable to find EERecHitCollection in event!\n";

   timers.pop_and_push("TrackAssociator::fillEcal::matching");
   std::set<DetId> ecalIdsInRegion = ecalDetIdAssociator_.getDetIdsCloseToAPoint(ecalTrajectory[0],idR);
   std::set<DetId> ecalIdsInACone =  ecalDetIdAssociator_.getDetIdsInACone(ecalIdsInRegion, ecalTrajectory, dR);
   std::set<DetId> crossedEcalIds =  ecalDetIdAssociator_.getCrossedDetIds(ecalIdsInRegion, ecalTrajectory);
   
   // add EcalRecHits
   timers.pop_and_push("TrackAssociator::fillEcal::addEcalRecHits");
   for(std::set<DetId>::const_iterator itr=crossedEcalIds.begin(); itr!=crossedEcalIds.end();itr++) {
     std::vector<EcalRecHit>::const_iterator hit = (*EBRecHits).find(*itr);
     if(hit != (*EBRecHits).end()) 
       info.crossedEcalRecHits.push_back(*hit);
     else  
       LogTrace("TrackAssociator::fillEcal") << "EcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
   }
   for(std::set<DetId>::const_iterator itr=ecalIdsInACone.begin(); itr!=ecalIdsInACone.end();itr++) {
     std::vector<EcalRecHit>::const_iterator hit = (*EBRecHits).find(*itr);
     if(hit != (*EBRecHits).end()) {
       info.coneEcalRecHits.push_back(*hit);
     }
     else 
       LogTrace("TrackAssociator::fillEcal") << "EcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
   }
   if (EERecHitCollectionLabels[1]==EBRecHitCollectionLabels[1])return;
   for(std::set<DetId>::const_iterator itr=crossedEcalIds.begin(); itr!=crossedEcalIds.end();itr++) {
     std::vector<EcalRecHit>::const_iterator hit = (*EERecHits).find(*itr);
     if(hit != (*EERecHits).end()) 
       info.crossedEcalRecHits.push_back(*hit);
     else  
       LogTrace("TrackAssociator::fillEcal") << "EcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
   }
   for(std::set<DetId>::const_iterator itr=ecalIdsInACone.begin(); itr!=ecalIdsInACone.end();itr++) {
     std::vector<EcalRecHit>::const_iterator hit = (*EERecHits).find(*itr);
     if(hit != (*EERecHits).end()) {
       info.coneEcalRecHits.push_back(*hit);
     }
     else 
       LogTrace("TrackAssociator::fillEcal") << "EcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
   }
}

//-----------------------------------------------------------------------------
void TrackAssociator::fillCaloTowers( const edm::Event& iEvent,
				      const edm::EventSetup& iSetup,
				      TrackDetMatchInfo& info,
				      const FreeTrajectoryState& trajectoryPoint,
				      const int idR,
				      const double dR)
{
   // ECAL hits are not used for the CaloTower identification
   TimerStack timers;
   timers.push("TrackAssociator::fillCaloTowers");

   caloDetIdAssociator_.setGeometry(&*theCaloGeometry_);
   
   // HCAL points (HB+HE)
   timers.push("TrackAssociator::fillCaloTowers::propagation");
   std::vector<GlobalPoint> hcalPoints;   
   hcalPoints.push_back(GlobalPoint(135.,0,310.));
   hcalPoints.push_back(GlobalPoint(150.,0,340.));
   hcalPoints.push_back(GlobalPoint(170.,0,370.));
   hcalPoints.push_back(GlobalPoint(190.,0,400.));
   hcalPoints.push_back(GlobalPoint(240.,0,500.));
   hcalPoints.push_back(GlobalPoint(280.,0,550.));
   
   std::vector<GlobalPoint> hcalTrajectory = caloDetIdAssociator_.getTrajectory(trajectoryPoint, hcalPoints);
//   if(hcalTrajectory.empty()) throw cms::Exception("FatalError") << "Failed to propagate the track to HCAL\n";

   if(hcalTrajectory.empty()) {
      LogTrace("TrackAssociator::fillEcal") << "Failed to propagate a track to ECAL; moving on\n";
      info.isGoodCalo = 0;
      info.isGoodEcal = 0;
      std::cout<<" TrackAssociator::fillCaloTowers::Failed to propagate a track to ECAL "<<std::endl;
      return;
   }
   
   info.isGoodCalo = 1;
   info.isGoodEcal = 1;
   info.trkGlobPosAtEcal = getPoint(hcalTrajectory[0]);
   
   if(hcalTrajectory.size()<4) {
      LogTrace("TrackAssociator::fillEcal") << "Failed to propagate a track to HCAL; moving on\n";
      info.isGoodHcal = 0;
   }
   
   info.isGoodHcal = 1;
   
   info.trkGlobPosAtHcal = getPoint(hcalTrajectory[4]);

   // find crossed CaloTowers
   timers.pop_and_push("TrackAssociator::fillCaloTowers::access::CaloTowers");
   edm::Handle<CaloTowerCollection> caloTowers;

   if (CaloTowerCollectionLabels.empty())
     throw cms::Exception("FatalError") << "Module lable is not set for CaloTowers.\n";
   else
     iEvent.getByLabel (CaloTowerCollectionLabels[0], CaloTowerCollectionLabels[1], caloTowers);
   if (!caloTowers.isValid())  throw cms::Exception("FatalError") << "Unable to find CaloTowers in event!\n";
   
   timers.push("TrackAssociator::fillCaloTowers::matching");

// first get DetIds in a predefined NxN region
//   std::set<DetId> caloTowerIdsInBigRegion = caloDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR+1);
   std::set<DetId> caloTowerIdsInRegion = caloDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR);

   std::set<DetId> caloTowerIdsInACone;
   std::set<DetId> crossedCaloTowerIds;
   std::set<DetId> caloTowerIdsInBox;
   caloTowerIdsInACone = caloDetIdAssociator_.getDetIdsInACone(caloTowerIdsInRegion, hcalTrajectory, dR);
// get DetId of the most energetic tower in that region
   crossedCaloTowerIds = caloDetIdAssociator_.getMaxEDetId(caloTowerIdsInRegion, caloTowers);
// get DetIds of the towers surrounding the most energetic one
   caloTowerIdsInBox = caloDetIdAssociator_.getDetIdsInACone(crossedCaloTowerIds, hcalTrajectory, -1.);

//
//  Debug prints
//
//   std::cout <<" Debug printout in CaloTowers "<<std::endl;
//   std::cout <<" with position at outer layer:r,z,phi "<<trajectoryPoint.position().eta()<<
//               " "<<trajectoryPoint.position().phi()<<
//	       " "<<trajectoryPoint.position().perp()<<
//               " "<<trajectoryPoint.position().z()<<
//	       " "<<trajectoryPoint.charge()<<std::endl;
//   std::cout <<" Trajectory point at ECAL surface:eta:phi:radius:z "<<(hcalTrajectory[0]).eta()<<
//               " "<<(hcalTrajectory[0]).phi()<<
//               " "<<(hcalTrajectory[0]).perp()<<
//	       " "<<(hcalTrajectory[0]).z()<<
//	       " momentum "<<trajectoryPoint.momentum().perp()<<std::endl; 
//
//   std::cout<<" Number of towers in the region "<<caloTowerIdsInRegion.size()<<" idR= "<<idR<<std::endl;
   
   // add CaloTowers
   timers.push("TrackAssociator::fillCaloTowers::addCaloTowers");
   for(std::set<DetId>::const_iterator itr=crossedCaloTowerIds.begin(); itr!=crossedCaloTowerIds.end();itr++)
     {
	DetId id(*itr);
	CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
	if(tower != (*caloTowers).end()) 
	  info.crossedTowers.push_back(*tower);
	else
	  LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }

   for(std::set<DetId>::const_iterator itr=caloTowerIdsInACone.begin(); itr!=caloTowerIdsInACone.end();itr++)
     {
	DetId id(*itr);
	CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
	if(tower != (*caloTowers).end()) {
	  info.coneTowers.push_back(*tower);
        }
	else 
	  LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }

   for(std::set<DetId>::const_iterator itr=caloTowerIdsInBox.begin(); itr!=caloTowerIdsInBox.end();itr++)
     {
        DetId id(*itr);
        CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
        if(tower != (*caloTowers).end()) {
          info.boxTowers.push_back(*tower);
        }
        else
          LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }
   
   for(std::set<DetId>::const_iterator itr=caloTowerIdsInRegion.begin(); itr!=caloTowerIdsInRegion.end();itr++)
     {
	DetId id(*itr);
	CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
	if(tower != (*caloTowers).end()) {
	  info.regionTowers.push_back(*tower);
        }
	else 
	  LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }

}

//-----------------------------------------------------------------------------
void TrackAssociator::fillHcal( const edm::Event& iEvent,
                                      const edm::EventSetup& iSetup,
                                      TrackDetMatchInfo& info,
                                      const FreeTrajectoryState& trajectoryPoint,
                                      const int idR,
                                      const double dR) {
  TimerStack timers;
  timers.push("TrackAssociator::fillHcal");

  hcalDetIdAssociator_.setGeometry(&*theCaloGeometry_);

// HCAL points (HB+HE)
  timers.push("TrackAssociator::fillHcal::propagation");
  std::vector<GlobalPoint> hcalPoints;
  hcalPoints.push_back(GlobalPoint(190.,0,400.));
  hcalPoints.push_back(GlobalPoint(240.,0,500.));
  hcalPoints.push_back(GlobalPoint(280.,0,550.));

  std::vector<GlobalPoint> hcalTrajectory = hcalDetIdAssociator_.getTrajectory(trajectoryPoint, hcalPoints);
//   if(hcalTrajectory.empty()) throw cms::Exception("FatalError") << "Failed to propagate the track to HCAL\n";

  if(hcalTrajectory.empty()) {
    LogTrace("TrackAssociator::fillHcal") << "Failed to propagate a track to HCAL; moving on\n";
    info.isGoodHcal = 0;
//    std::cout<<" TrackAssociator::fillHcal::Failed to propagate a track to HCAL "<<std::endl;
    return;
  }

  info.isGoodHcal = 1;

  info.trkGlobPosAtHcal = getPoint(hcalTrajectory[0]);
  timers.pop_and_push("TrackAssociator::fillHcal::access::Hcal");

  edm::Handle<HBHERecHitCollection> HBHERecHits;
//  if (HBHERecHitCollectionLabels.empty())
//    throw cms::Exception("FatalError") << "Module label is not set for HBHERecHitCollection.\n";
//  else
    iEvent.getByLabel (HBHERecHitCollectionLabels[0], HBHERecHitCollectionLabels[1], HBHERecHits);
  if (!HBHERecHits.isValid()) throw cms::Exception("FatalError") << "Unable to find HBHERecHitCollection in event!\n";

  timers.pop_and_push("TrackAssociator::fillHcal::matching");

// first get DetIds in a predefined NxN region
//  std::set<DetId> hcalIdsInBigRegion = hcalDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR+1);
  std::set<DetId> hcalIdsInRegion = hcalDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR);

  std::set<DetId> hcalIdsInACone;
  std::set<DetId> crossedHcalIds;
  std::set<DetId> hcalIdsInBox;
  hcalIdsInACone = hcalDetIdAssociator_.getDetIdsInACone(hcalIdsInRegion, hcalTrajectory, dR);
// get DetId of the most energetic tower in that region
  crossedHcalIds = hcalDetIdAssociator_.getMaxEDetId(hcalIdsInRegion, HBHERecHits);
// get DetIds of the towers surrounding the most energetic one
  hcalIdsInBox = hcalDetIdAssociator_.getDetIdsInACone(crossedHcalIds, hcalTrajectory, -1.);

// add HcalRecHits
  timers.pop_and_push("TrackAssociator::fillHcal::addHcalRecHits");
  for(std::set<DetId>::const_iterator itr=crossedHcalIds.begin(); itr!=crossedHcalIds.end();itr++) {
    std::vector<HBHERecHit>::const_iterator hit = (*HBHERecHits).find(*itr);
    if(hit != (*HBHERecHits).end())
      info.crossedHcalRecHits.push_back(*hit);
    else
      LogTrace("TrackAssociator::fillHcal") << "HcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
  }
  for(std::set<DetId>::const_iterator itr=hcalIdsInACone.begin(); itr!=hcalIdsInACone.end();itr++) {
    std::vector<HBHERecHit>::const_iterator hit = (*HBHERecHits).find(*itr);
    if(hit != (*HBHERecHits).end())
      info.coneHcalRecHits.push_back(*hit);
    else
      LogTrace("TrackAssociator::fillHcal") << "HcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
  }
  for(std::set<DetId>::const_iterator itr=hcalIdsInBox.begin(); itr!=hcalIdsInBox.end();itr++) {
    std::vector<HBHERecHit>::const_iterator hit = (*HBHERecHits).find(*itr);
    if(hit != (*HBHERecHits).end())
      info.boxHcalRecHits.push_back(*hit);
    else
      LogTrace("TrackAssociator::fillHcal") << "HcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
  }
  for(std::set<DetId>::const_iterator itr=hcalIdsInRegion.begin(); itr!=hcalIdsInRegion.end();itr++) {
    std::vector<HBHERecHit>::const_iterator hit = (*HBHERecHits).find(*itr);
    if(hit != (*HBHERecHits).end())
      info.regionHcalRecHits.push_back(*hit);
    else
      LogTrace("TrackAssociator::fillHcal") << "HcalRecHit is not found for DetId: " << itr->rawId() <<"\n";
  }
}

//-----------------------------------------------------------------------------
void TrackAssociator::fillHcalTowers( const edm::Event& iEvent,
				      const edm::EventSetup& iSetup,
				      TrackDetMatchInfo& info,
				      const FreeTrajectoryState& trajectoryPoint,
				      const int idR,
				      const double dR)
{
   // ECAL hits are not used for the CaloTower identification
   TimerStack timers;
   timers.push("TrackAssociator::fillCaloTowers");

   caloDetIdAssociator_.setGeometry(&*theCaloGeometry_);
   
   // HCAL points (HB+HE)
   timers.push("TrackAssociator::fillCaloTowers::propagation");
   std::vector<GlobalPoint> hcalPoints;
   hcalPoints.push_back(GlobalPoint(190.,0,400.));
   hcalPoints.push_back(GlobalPoint(240.,0,500.));
   hcalPoints.push_back(GlobalPoint(280.,0,550.));
   
   std::vector<GlobalPoint> hcalTrajectory = caloDetIdAssociator_.getTrajectory(trajectoryPoint, hcalPoints);
//   if(hcalTrajectory.empty()) throw cms::Exception("FatalError") << "Failed to propagate the track to HCAL\n";

   if(hcalTrajectory.empty()) {
      LogTrace("TrackAssociator::fillEcal") << "Failed to propagate a track to HCAL; moving on\n";
      info.isGoodCalo = 0;
      std::cout<<" TrackAssociator::fillCaloTowers::Failed to propagate a track to HCAL "<<std::endl;
      return;
   }
   
   info.isGoodCalo = 1;

   info.trkGlobPosAtHcal = getPoint(hcalTrajectory[0]);
   
   // find crossed CaloTowers
   timers.pop_and_push("TrackAssociator::fillCaloTowers::access::CaloTowers");
   edm::Handle<CaloTowerCollection> caloTowers;

   if (CaloTowerCollectionLabels.empty())
     throw cms::Exception("FatalError") << "Module lable is not set for CaloTowers.\n";
   else
     iEvent.getByLabel (CaloTowerCollectionLabels[0], CaloTowerCollectionLabels[1], caloTowers);
   if (!caloTowers.isValid())  throw cms::Exception("FatalError") << "Unable to find CaloTowers in event!\n";
   
   timers.push("TrackAssociator::fillCaloTowers::matching");

// first get DetIds in a predefined NxN region
   std::set<DetId> caloTowerIdsInBigRegion = caloDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR+1);
   std::set<DetId> caloTowerIdsInRegion = caloDetIdAssociator_.getDetIdsCloseToAPoint(hcalTrajectory[0],idR);

   std::set<DetId> caloTowerIdsInACone;
   std::set<DetId> crossedCaloTowerIds;
   std::set<DetId> caloTowerIdsInBox;
   caloTowerIdsInACone = caloDetIdAssociator_.getDetIdsInACone(caloTowerIdsInBigRegion, hcalTrajectory, dR);
// get DetId of the most energetic tower in that region
   crossedCaloTowerIds = caloDetIdAssociator_.getMaxEDetId(caloTowerIdsInRegion, caloTowers);
// get DetIds of the towers surrounding the most energetic one
   caloTowerIdsInBox = caloDetIdAssociator_.getDetIdsInACone(crossedCaloTowerIds, hcalTrajectory, -1.);
   
   // add CaloTowers
   timers.push("TrackAssociator::fillCaloTowers::addCaloTowers");
   for(std::set<DetId>::const_iterator itr=crossedCaloTowerIds.begin(); itr!=crossedCaloTowerIds.end();itr++)
     {
	DetId id(*itr);
	CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
	if(tower != (*caloTowers).end()) 
	  info.crossedTowers.push_back(*tower);
	else
	  LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }

   for(std::set<DetId>::const_iterator itr=caloTowerIdsInACone.begin(); itr!=caloTowerIdsInACone.end();itr++)
     {
	DetId id(*itr);
	CaloTowerCollection::const_iterator tower = (*caloTowers).find(id);
	if(tower != (*caloTowers).end()) 
	  info.coneTowers.push_back(*tower);
	else 
	  LogTrace("TrackAssociator::fillEcal") << "CaloTower is not found for DetId: " << id.rawId() << "\n";
     }
   
}

//-----------------------------------------------------------------------------
FreeTrajectoryState TrackAssociator::getFreeTrajectoryState( const edm::EventSetup& iSetup, 
							     const SimTrack& track, 
							     const SimVertex& vertex )
{
   edm::ESHandle<MagneticField> bField;
   iSetup.get<IdealMagneticFieldRecord>().get(bField);
   
   GlobalVector vector( track.momentum().x(), track.momentum().y(), track.momentum().z() );
   // convert mm to cm
   GlobalPoint point( vertex.position().x()*.1, vertex.position().y()*.1, vertex.position().z()*.1 );
   int charge = track.type( )> 0 ? -1 : 1;
   GlobalTrajectoryParameters tPars(point, vector, charge, &*bField);
   
   HepSymMatrix covT(6,1); covT *= 1e-6; // initialize to sigma=1e-3
   CartesianTrajectoryError tCov(covT);
   
   return FreeTrajectoryState(tPars, tCov);
}

//-----------------------------------------------------------------------------
FreeTrajectoryState TrackAssociator::getFreeTrajectoryState( const edm::EventSetup& iSetup,
							     const reco::Track& track )
{
   edm::ESHandle<MagneticField> bField;
   iSetup.get<IdealMagneticFieldRecord>().get(bField);
   
   GlobalVector vector( track.momentum().x(), track.momentum().y(), track.momentum().z() );

   GlobalPoint point( track.vertex().x(), track.vertex().y(),  track.vertex().z() );

   GlobalTrajectoryParameters tPars(point, vector, track.charge(), &*bField);
   
   // FIX THIS !!!
   // need to convert from perigee to global or helix (curvilinear) frame
   // for now just an arbitrary matrix.
   HepSymMatrix covT(6,1); covT *= 1e-6; // initialize to sigma=1e-3
   CartesianTrajectoryError tCov(covT);
   
   return FreeTrajectoryState(tPars, tCov);
}

