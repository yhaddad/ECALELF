#include <vector>
#include <memory>
#include <algorithm>
#include <cmath>

// Class header file
#include "Calibration/HcalIsolatedTrackReco/interface/IsolatedPixelTrackCandidateProducer.h"
#include "DataFormats/HcalIsolatedTrack/interface/IsolatedPixelTrackCandidateFwd.h"
// Framework
#include "DataFormats/Common/interface/Handle.h"
#include "FWCore/Framework/interface/ESHandle.h"
#include "FWCore/Framework/interface/ESTransientHandle.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"
#include "FWCore/Utilities/interface/Exception.h"
//


// Math
#include "Math/GenVector/VectorUtil.h"
#include "Math/GenVector/PxPyPzE4D.h"
#include "DataFormats/Math/interface/deltaR.h"

#include "Geometry/Records/interface/IdealGeometryRecord.h"
#include "DetectorDescription/Core/interface/DDLogicalPart.h"
#include "DetectorDescription/Core/interface/DDSolid.h"

//magF
#include "MagneticField/Engine/interface/MagneticField.h"
#include "MagneticField/Records/interface/IdealMagneticFieldRecord.h"
#include "MagneticField/VolumeBasedEngine/interface/VolumeBasedMagneticField.h"

//for ECAL geometry
#include "Geometry/EcalAlgo/interface/EcalBarrelGeometry.h"
#include "Geometry/EcalAlgo/interface/EcalEndcapGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloSubdetectorGeometry.h"
#include "Geometry/CaloGeometry/interface/CaloGeometry.h"
#include "Geometry/Records/interface/CaloGeometryRecord.h"


IsolatedPixelTrackCandidateProducer::IsolatedPixelTrackCandidateProducer(const edm::ParameterSet& config){
   
  tok_l1_ = consumes<l1extra::L1JetParticleCollection>(config.getParameter<edm::InputTag>("L1eTauJetsSource"));
  tauAssocCone_               = config.getParameter<double>("tauAssociationCone"); 
  tauUnbiasCone_              = config.getParameter<double>("tauUnbiasCone");
  pixelTracksSources_         = config.getParameter<std::vector<edm::InputTag> >("PixelTracksSources");
  const unsigned nLabels = pixelTracksSources_.size();
  for ( unsigned i=0; i != nLabels; i++ ) 
    toks_pix_.push_back(consumes<reco::TrackCollection>(pixelTracksSources_[i]));
  prelimCone_                 = config.getParameter<double>("ExtrapolationConeSize");
  pixelIsolationConeSizeAtEC_ = config.getParameter<double>("PixelIsolationConeSizeAtEC");
  tok_hlt_ = consumes<trigger::TriggerFilterObjectWithRefs>(config.getParameter<edm::InputTag>("L1GTSeedLabel"));
  vtxCutSeed_                 = config.getParameter<double>("MaxVtxDXYSeed");
  vtxCutIsol_                 = config.getParameter<double>("MaxVtxDXYIsol");
  tok_vert_ = consumes<reco::VertexCollection>(config.getParameter<edm::InputTag>("VertexLabel"));
  bfield_                     = config.getParameter<std::string>("MagFieldRecordName");
  minPTrackValue_             = config.getParameter<double>("minPTrack");
  maxPForIsolationValue_      = config.getParameter<double>("maxPTrackForIsolation");
  ebEtaBoundary_              = config.getParameter<double>("EBEtaBoundary");
  rEB_ = zEE_ = -1;
  bfVal = 0;
  // Register the product
  produces< reco::IsolatedPixelTrackCandidateCollection >();
}

IsolatedPixelTrackCandidateProducer::~IsolatedPixelTrackCandidateProducer() {

}

void IsolatedPixelTrackCandidateProducer::beginJob () {}

void IsolatedPixelTrackCandidateProducer::beginRun(const edm::Run &run, const edm::EventSetup &theEventSetup) {

  edm::ESHandle<CaloGeometry> pG;
  theEventSetup.get<CaloGeometryRecord>().get(pG);   
  
  const double rad (dynamic_cast<const EcalBarrelGeometry*>( pG->getSubdetectorGeometry(DetId::Ecal, EcalBarrel ))->avgRadiusXYFrontFaceCenter() ) ;
  
  const double zz (dynamic_cast<const EcalEndcapGeometry*>( pG->getSubdetectorGeometry(DetId::Ecal, EcalEndcap ))->avgAbsZFrontFaceCenter() ) ;

  rEB_=rad;
  zEE_=zz;

  edm::ESHandle<MagneticField> vbfField;
  theEventSetup.get<IdealMagneticFieldRecord>().get(vbfField);
  const VolumeBasedMagneticField* vbfCPtr = dynamic_cast<const VolumeBasedMagneticField*>(&(*vbfField));
  GlobalVector BField=vbfCPtr->inTesla(GlobalPoint(0,0,0));
  bfVal=BField.mag();
}

void IsolatedPixelTrackCandidateProducer::produce(edm::Event& theEvent, const edm::EventSetup& theEventSetup) {

  reco::IsolatedPixelTrackCandidateCollection* trackCollection=new reco::IsolatedPixelTrackCandidateCollection;

  //create vector of refs from input collections
  std::vector<reco::TrackRef> pixelTrackRefs;

  for (unsigned int iPix=0; iPix<pixelTracksSources_.size(); iPix++) {
    edm::Handle<reco::TrackCollection> iPixCol;
    theEvent.getByToken(toks_pix_[iPix],iPixCol);
    for (reco::TrackCollection::const_iterator pit=iPixCol->begin(); pit!=iPixCol->end(); pit++) {
      pixelTrackRefs.push_back(reco::TrackRef(iPixCol,pit-iPixCol->begin()));
    }
  }

  edm::Handle<l1extra::L1JetParticleCollection> l1eTauJets;
  theEvent.getByToken(tok_l1_,l1eTauJets);

  edm::Handle<reco::VertexCollection> pVert;
  theEvent.getByToken(tok_vert_,pVert);

  double ptTriggered  = -10;
  double etaTriggered = -100;
  double phiTriggered = -100;
  
  edm::Handle<trigger::TriggerFilterObjectWithRefs> l1trigobj;
  theEvent.getByToken(tok_hlt_, l1trigobj);
  
  std::vector< edm::Ref<l1extra::L1JetParticleCollection> > l1tauobjref;
  std::vector< edm::Ref<l1extra::L1JetParticleCollection> > l1jetobjref;
  std::vector< edm::Ref<l1extra::L1JetParticleCollection> > l1forjetobjref;
  
  l1trigobj->getObjects(trigger::TriggerL1TauJet, l1tauobjref);
  l1trigobj->getObjects(trigger::TriggerL1CenJet, l1jetobjref);
  l1trigobj->getObjects(trigger::TriggerL1ForJet, l1forjetobjref);
  
  for (unsigned int p=0; p<l1tauobjref.size(); p++) {
    if (l1tauobjref[p]->pt()>ptTriggered) {
      ptTriggered  = l1tauobjref[p]->pt(); 
      phiTriggered = l1tauobjref[p]->phi();
      etaTriggered = l1tauobjref[p]->eta();
    }
  }
  for (unsigned int p=0; p<l1jetobjref.size(); p++) {
    if (l1jetobjref[p]->pt()>ptTriggered) {
      ptTriggered  = l1jetobjref[p]->pt();
      phiTriggered = l1jetobjref[p]->phi();
      etaTriggered = l1jetobjref[p]->eta();
    }
  }
  for (unsigned int p=0; p<l1forjetobjref.size(); p++) {
    if (l1forjetobjref[p]->pt()>ptTriggered) {
      ptTriggered=l1forjetobjref[p]->pt();
      phiTriggered=l1forjetobjref[p]->phi();
      etaTriggered=l1forjetobjref[p]->eta();
    }
  }

  double drMaxL1Track_ = tauAssocCone_;
  
  int ntr = 0;
  std::vector<seedAtEC>  VecSeedsatEC;
  //loop to select isolated tracks
  for (unsigned iS=0; iS<pixelTrackRefs.size(); iS++) {
    bool vtxMatch = false;
    //associate to vertex (in Z) 
    reco::VertexCollection::const_iterator vitSel;
    double minDZ = 1000;
    bool   found(false);
    for (reco::VertexCollection::const_iterator vit=pVert->begin(); vit!=pVert->end(); vit++) {
      if (std::abs(pixelTrackRefs[iS]->dz(vit->position()))<minDZ) {
	minDZ  = std::abs(pixelTrackRefs[iS]->dz(vit->position()));
	vitSel = vit;
	found  = true;
      }
    }
    //cut on dYX:
    if (found) {
      if(std::abs(pixelTrackRefs[iS]->dxy(vitSel->position()))<vtxCutSeed_) vtxMatch=true;
    } else {
      vtxMatch=true;
    }

    //select tracks not matched to triggered L1 jet
    double R=reco::deltaR(etaTriggered, phiTriggered, 
			  pixelTrackRefs[iS]->eta(), pixelTrackRefs[iS]->phi());
    if (R<tauUnbiasCone_) continue;

    //check taujet matching
    bool tmatch=false;
    l1extra::L1JetParticleCollection::const_iterator selj;
    for (l1extra::L1JetParticleCollection::const_iterator tj=l1eTauJets->begin(); tj!=l1eTauJets->end(); tj++) {
      if (reco::deltaR(pixelTrackRefs[iS]->momentum().eta(), pixelTrackRefs[iS]->momentum().phi(), tj->momentum().eta(), tj->momentum().phi()) > drMaxL1Track_) continue;
      selj   = tj;
      tmatch = true;
    } //loop over L1 tau

    
    //propagate seed track to ECAL surface:
    std::pair<double,double> seedCooAtEC;
    // in case vertex is found:
    if (found) seedCooAtEC=GetEtaPhiAtEcal(pixelTrackRefs[iS]->eta(), pixelTrackRefs[iS]->phi(), pixelTrackRefs[iS]->pt(), pixelTrackRefs[iS]->charge(), vitSel->z());
    //in case vertex is not found:
    else       seedCooAtEC=GetEtaPhiAtEcal(pixelTrackRefs[iS]->eta(), pixelTrackRefs[iS]->phi(), pixelTrackRefs[iS]->pt(), pixelTrackRefs[iS]->charge(), 0);
    seedAtEC seed(iS,(tmatch||vtxMatch),seedCooAtEC.first,seedCooAtEC.second);
    VecSeedsatEC.push_back(seed);
  }

  for (unsigned int i=0; i<VecSeedsatEC.size(); i++) {
    unsigned int iSeed = VecSeedsatEC[i].index;
    if (!VecSeedsatEC[i].ok) continue;
    if(pixelTrackRefs[iSeed]->p()<minPTrackValue_) continue; 
    l1extra::L1JetParticleCollection::const_iterator selj;
    for (l1extra::L1JetParticleCollection::const_iterator tj=l1eTauJets->begin(); tj!=l1eTauJets->end(); tj++)  {
      if (reco::deltaR(pixelTrackRefs[iSeed]->momentum().eta(),pixelTrackRefs[iSeed]->momentum().phi(),tj->momentum().eta(),tj->momentum().phi()) > drMaxL1Track_) continue;
      selj   = tj;
    } //loop over L1 tau
    double maxP = 0;
    double sumP = 0;
    for(unsigned int j=0; j<VecSeedsatEC.size(); j++) {
      if (i==j) continue;
      unsigned int iSurr = VecSeedsatEC[j].index;
      //define preliminary cone around seed track impact point from which tracks will be extrapolated:
      if (reco::deltaR(pixelTrackRefs[iSeed]->eta(), pixelTrackRefs[iSeed]->phi(), pixelTrackRefs[iSurr]->eta(), pixelTrackRefs[iSurr]->phi())>prelimCone_) continue;
      double minDZ2(1000);
      bool   found(false);
      reco::VertexCollection::const_iterator vitSel2;
      for (reco::VertexCollection::const_iterator vit=pVert->begin(); vit!=pVert->end(); vit++) {
	if (std::abs(pixelTrackRefs[iSurr]->dz(vit->position()))<minDZ2) {
	  minDZ2  = std::abs(pixelTrackRefs[iSurr]->dz(vit->position()));
	  vitSel2 = vit;
	  found   = true;
	}
      }
      //cut ot dXY:
      if (found&&std::abs(pixelTrackRefs[iSurr]->dxy(vitSel2->position()))>vtxCutIsol_) continue;
      //calculate distance at ECAL surface and update isolation: 
      if (getDistInCM(VecSeedsatEC[i].eta, VecSeedsatEC[i].phi, VecSeedsatEC[j].eta, VecSeedsatEC[j].phi)<pixelIsolationConeSizeAtEC_) {
	sumP+=pixelTrackRefs[iSurr]->p();
	if(pixelTrackRefs[iSurr]->p()>maxP) maxP=pixelTrackRefs[iSurr]->p();
      }      
    }
    if (maxP<maxPForIsolationValue_) {
      reco::IsolatedPixelTrackCandidate newCandidate(pixelTrackRefs[iSeed], l1extra::L1JetParticleRef(l1eTauJets,selj-l1eTauJets->begin()), maxP, sumP);
      newCandidate.SetEtaPhiEcal(VecSeedsatEC[i].eta, VecSeedsatEC[i].phi);
      trackCollection->push_back(newCandidate);
      ntr++;
    }    
  }
  // put the product in the event
  std::auto_ptr< reco::IsolatedPixelTrackCandidateCollection > outCollection(trackCollection);
  theEvent.put(outCollection);
}


double IsolatedPixelTrackCandidateProducer::getDistInCM(double eta1, double phi1, double eta2, double phi2) {
  double Rec;
  double theta1=2*atan(exp(-eta1));
  double theta2=2*atan(exp(-eta2));
  if (std::abs(eta1)<1.479) Rec=rEB_; //radius of ECAL barrel
  else if (std::abs(eta1)>1.479&&std::abs(eta1)<7.0) Rec=tan(theta1)*zEE_; //distance from IP to ECAL endcap
  else return 1000;

  //|vect| times tg of acos(scalar product)
  double angle=acos((sin(theta1)*sin(theta2)*(sin(phi1)*sin(phi2)+cos(phi1)*cos(phi2))+cos(theta1)*cos(theta2)));
  if (angle<M_PI_2) return std::abs((Rec/sin(theta1))*tan(angle));
  else return 1000;
}


std::pair<double,double> IsolatedPixelTrackCandidateProducer::GetEtaPhiAtEcal(double etaIP, double phiIP, double pT, int charge, double vtxZ) {

  double deltaPhi=0;
  double etaEC = 100;
  double phiEC = 100;

  double Rcurv = 9999999;
  if (bfVal!=0) Rcurv=pT*33.3*100/(bfVal*10); //r(m)=pT(GeV)*33.3/B(kG)

  double ecDist = zEE_;  //distance to ECAL andcap from IP (cm), 317 - ecal (not preshower), preshower -300
  double ecRad  = rEB_;  //radius of ECAL barrel (cm)
  double theta  = 2*atan(exp(-etaIP));
  double zNew   = 0;
  if (theta>M_PI_2) theta=M_PI-theta;
  if (std::abs(etaIP)<ebEtaBoundary_) {
    if ((0.5*ecRad/Rcurv)>1) {
      etaEC         = 10000;
      deltaPhi      = 0;
    } else {
      deltaPhi      =-charge*asin(0.5*ecRad/Rcurv);
      double alpha1 = 2*asin(0.5*ecRad/Rcurv);
      double z      = ecRad/tan(theta);
      if (etaIP>0) zNew = z*(Rcurv*alpha1)/ecRad+vtxZ; //new z-coordinate of track
      else         zNew =-z*(Rcurv*alpha1)/ecRad+vtxZ; //new z-coordinate of track
      double zAbs=std::abs(zNew);
      if (zAbs<ecDist) {
	etaEC    = -log(tan(0.5*atan(ecRad/zAbs)));
	deltaPhi = -charge*asin(0.5*ecRad/Rcurv);
      }
      if (zAbs>ecDist) {
	zAbs           = (std::abs(etaIP)/etaIP)*ecDist;
	double Zflight = std::abs(zAbs-vtxZ);
	double alpha   = (Zflight*ecRad)/(z*Rcurv);
	double Rec     = 2*Rcurv*sin(alpha/2);
	deltaPhi       =-charge*alpha/2;
	etaEC          =-log(tan(0.5*atan(Rec/ecDist)));
      }
    }
  } else {
    zNew           = (std::abs(etaIP)/etaIP)*ecDist;
    double Zflight = std::abs(zNew-vtxZ);
    double Rvirt   = std::abs(Zflight*tan(theta));
    double Rec     = 2*Rcurv*sin(Rvirt/(2*Rcurv));
    deltaPhi       =-(charge)*(Rvirt/(2*Rcurv));
    etaEC          =-log(tan(0.5*atan(Rec/ecDist)));
  }

  if (zNew<0) etaEC=-etaEC;
  phiEC            = phiIP+deltaPhi;

  if (phiEC<-M_PI) phiEC += M_2_PI;
  if (phiEC>M_PI)  phiEC -= M_2_PI;

  std::pair<double,double> retVal(etaEC,phiEC);
  return retVal;
}


