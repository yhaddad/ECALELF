#include "Calibration/EcalAlCaRecoProducers/plugins/AlCaECALRecHitReducer.h"
//#include "DataFormats/EgammaCandidates/interface/GsfElectron.h"
#include "DataFormats/EgammaCandidates/interface/Photon.h"
#include "FWCore/MessageLogger/interface/MessageLogger.h"

#include "DataFormats/EgammaReco/interface/BasicCluster.h"
#include "DataFormats/EcalDetId/interface/EcalSubdetector.h"
#include "Geometry/CaloEventSetup/interface/CaloTopologyRecord.h"
#include "FWCore/Framework/interface/ESHandle.h"

//#define ALLrecHits
//#define DEBUG

//#define QUICK -> if commented loop over the recHits of the SC and add them to the list of recHits to be saved
//                 comment it if you want a faster module but be sure the window is large enough

/** This module reduces the recHitCollections and the caloCalusterCollections in input
 * keeping only those associated to the given electrons/photons
 */

/// \todo make sure that the new caloClusterCollection has no duplicates

AlCaECALRecHitReducer::AlCaECALRecHitReducer(const edm::ParameterSet& iConfig)
{

	ebRecHitsToken_ = consumes<EcalRecHitCollection>(iConfig.getParameter< edm::InputTag > ("ebRecHitsLabel"));
	eeRecHitsToken_ = consumes<EcalRecHitCollection>(iConfig.getParameter< edm::InputTag > ("eeRecHitsLabel"));
	esRecHitsToken_ = consumes<ESRecHitCollection>(  iConfig.getParameter< edm::InputTag > ("esRecHitsLabel"));
	ebUncalibRecHitsToken_ = consumes<EcalUncalibratedRecHitCollection>(iConfig.getParameter< edm::InputTag > ("uncalibRecHitCollectionEB"));
	eeUncalibRecHitsToken_ = consumes<EcalUncalibratedRecHitCollection>(iConfig.getParameter< edm::InputTag > ("uncalibRecHitCollectionEE"));


	std::vector<edm::InputTag> srcLabels = iConfig.getParameter< std::vector<edm::InputTag> >("srcLabels");
	for ( auto inputTag = srcLabels.begin(); inputTag != srcLabels.end(); ++inputTag) {
	  eleViewTokens_.push_back(consumes<edm::View <reco::RecoCandidate> >(*inputTag));
	}

	//eleViewToken_ = consumes<edm::View <reco::RecoCandidate> > (iConfig.getParameter< edm::InputTag > ("electronLabel"));
	photonToken_ = consumes<reco::PhotonCollection>(iConfig.getParameter< edm::InputTag > ("photonLabel"));
	EESuperClusterToken_ = consumes<reco::SuperClusterCollection>(iConfig.getParameter< edm::InputTag>("EESuperClusterCollection"));

	minEta_highEtaSC_         = iConfig.getParameter< double >("minEta_highEtaSC");

	alcaBarrelHitsCollection_  = iConfig.getParameter<std::string>("alcaBarrelHitCollection");
	alcaEndcapHitsCollection_  = iConfig.getParameter<std::string>("alcaEndcapHitCollection");
	alcaPreshowerHitsCollection_ = iConfig.getParameter<std::string>("alcaPreshowerHitCollection");
	alcaBarrelUncalibHitsCollection_ = iConfig.getParameter<std::string>("alcaBarrelUncalibHitCollection");
	alcaEndcapUncalibHitsCollection_ = iConfig.getParameter<std::string>("alcaEndcapUncalibHitCollection");
	alcaCaloClusterCollection_ = iConfig.getParameter<std::string>("alcaCaloClusterCollection");


	etaSize_ = iConfig.getParameter<int> ("etaSize");
	phiSize_ = iConfig.getParameter<int> ("phiSize");
	// FIXME: minimum size of etaSize_ and phiSize_
	if ( phiSize_ % 2 == 0 ||  etaSize_ % 2 == 0)
		edm::LogError("AlCaECALRecHitReducerError") << "Size of eta/phi should be odd numbers";

	//  esNstrips_  = iConfig.getParameter<int> ("esNstrips");
	//  esNcolumns_ = iConfig.getParameter<int> ("esNcolumns");

	//register your products
	produces< EBRecHitCollection > (alcaBarrelHitsCollection_) ;
	produces< EERecHitCollection > (alcaEndcapHitsCollection_) ;
	produces< ESRecHitCollection > (alcaPreshowerHitsCollection_) ;
	produces< EBUncalibratedRecHitCollection > (alcaBarrelUncalibHitsCollection_) ;
	produces< EEUncalibratedRecHitCollection > (alcaEndcapUncalibHitsCollection_) ;

	produces< reco::CaloClusterCollection > (alcaCaloClusterCollection_) ;

}


AlCaECALRecHitReducer::~AlCaECALRecHitReducer()
{}


// ------------ method called to produce the data  ------------
void
AlCaECALRecHitReducer::produce (edm::Event& iEvent,
                                const edm::EventSetup& iSetup)
{
	using namespace edm;
	//using namespace std;

	EcalRecHitCollection::const_iterator recHit_itr;

	edm::ESHandle<CaloTopology> theCaloTopology;
	iSetup.get<CaloTopologyRecord>().get(theCaloTopology);
	const CaloTopology *caloTopology = theCaloTopology.product();


	// Get Photons
	Handle<reco::PhotonCollection> phoHandle;
	iEvent.getByToken(photonToken_, phoHandle);

	// get RecHits
	Handle<EBRecHitCollection> barrelRecHitsHandle;
	iEvent.getByToken(ebRecHitsToken_, barrelRecHitsHandle);
	const EBRecHitCollection *barrelHitsCollection = barrelRecHitsHandle.product () ;

	// get RecHits
	Handle<EERecHitCollection> endcapRecHitsHandle;
	iEvent.getByToken(eeRecHitsToken_, endcapRecHitsHandle);
	const EERecHitCollection *endcapHitsCollection = endcapRecHitsHandle.product () ;

	// get UncalibRecHits
	Handle<EBUncalibratedRecHitCollection> barrelUncalibRecHitsHandle;
	iEvent.getByToken(ebUncalibRecHitsToken_, barrelUncalibRecHitsHandle);
	const EBUncalibratedRecHitCollection *barrelUncalibHitsCollection = barrelUncalibRecHitsHandle.isValid() ? barrelUncalibRecHitsHandle.product () : NULL;

	// get UncalibRecHits
	Handle<EEUncalibratedRecHitCollection> endcapUncalibRecHitsHandle;
	iEvent.getByToken(eeUncalibRecHitsToken_, endcapUncalibRecHitsHandle);
	const EEUncalibratedRecHitCollection *endcapUncalibHitsCollection = endcapUncalibRecHitsHandle.isValid() ?  endcapUncalibRecHitsHandle.product () : NULL;


	// get ES RecHits
	Handle<ESRecHitCollection> preshowerRecHitsHandle;
	iEvent.getByToken(esRecHitsToken_, preshowerRecHitsHandle);
	const ESRecHitCollection * preshowerHitsCollection  = preshowerRecHitsHandle.product () ;
	//   if (preshowerIsFull)
	//

	//   // make a vector to store the used ES rechits:
	//   set<ESDetId> used_strips;
	//   used_strips.clear();

	// for Z->ele+SC
	Handle<reco::SuperClusterCollection> EESCHandle;
	iEvent.getByToken(EESuperClusterToken_, EESCHandle);

	//Create empty output collections
	std::unique_ptr< EBRecHitCollection > miniEBRecHitCollection (new EBRecHitCollection) ;
	std::unique_ptr< EERecHitCollection > miniEERecHitCollection (new EERecHitCollection) ;
	std::unique_ptr< ESRecHitCollection > miniESRecHitCollection (new ESRecHitCollection) ;
	std::unique_ptr< EBUncalibratedRecHitCollection > miniEBUncalibRecHitCollection (new EBUncalibratedRecHitCollection) ;
	std::unique_ptr< EEUncalibratedRecHitCollection > miniEEUncalibRecHitCollection (new EEUncalibratedRecHitCollection) ;

	std::set<DetId> reducedRecHit_EBmap;
	std::set<DetId> reducedRecHit_EEmap;
	std::set<DetId> reducedRecHit_ESmap;

	//  std::set< edm::Ref<reco::CaloCluster> > reducedCaloClusters_map;

	std::unique_ptr< reco::CaloClusterCollection > reducedCaloClusterCollection (new reco::CaloClusterCollection);

	//Photons:
#ifdef shervin
	for (reco::PhotonCollection::const_iterator phoIt = phoHandle->begin(); phoIt != phoHandle->end(); phoIt++) {
		const reco::SuperCluster& sc = *(phoIt->superCluster()) ;

		if (phoIt->isEB()) {
			AddMiniRecHitCollection(sc, reducedRecHit_EBmap, caloTopology);
		} else { // endcap
			AddMiniRecHitCollection(sc, reducedRecHit_EEmap, caloTopology);
		} // end of endcap

		/// \todo check if this works when you ask sc->seed(), I suspect that the references have to be updated
		reco::CaloCluster_iterator it = sc.clustersBegin();
		reco::CaloCluster_iterator itend = sc.clustersEnd();
		for ( ; it != itend; ++it) {
			reco::CaloCluster caloClus(**it);
			reducedCaloClusterCollection->push_back(caloClus);
		}
	}
#endif

	Handle<edm::View < reco::RecoCandidate> > eleViewHandle;
	for( auto iToken = eleViewTokens_.begin(); iToken != eleViewTokens_.end(); iToken++) {
		iEvent.getByToken(*iToken, eleViewHandle);

		//Electrons:
		for ( auto eleIt = eleViewHandle->begin(); eleIt != eleViewHandle->end(); eleIt++) {
			const reco::SuperCluster& sc = *(eleIt->superCluster()) ;

			if (sc.seed()->seed().subdetId() == EcalBarrel) {
				// Fixed from if (|sc.eta|<1.479)
				// This is not case because in some rare cases the SC eta is > 1.479 for SCs
				// in the EB collection. The reason for this is unclear and should be checked
				// This caused a seg fault later on, because the corresponding rechits were
				// injected into the EE collection rather than the EB collection.
				// Checking teh subdetID should be safe, make sure to propagate to full CMSSW
				// FIXME
				AddMiniRecHitCollection(sc, reducedRecHit_EBmap, caloTopology, reducedRecHit_ESmap);
			} else { // endcap
				AddMiniRecHitCollection(sc, reducedRecHit_EEmap, caloTopology, reducedRecHit_ESmap);
			} // end of endcap

			reco::CaloCluster_iterator it = sc.clustersBegin();
			reco::CaloCluster_iterator itend = sc.clustersEnd();
			for ( ; it != itend; ++it) {
				reco::CaloCluster caloClus(**it);
				reducedCaloClusterCollection->push_back(caloClus);
			}
		}
	}



	//saving recHits for highEta SCs for highEta studies
	for(reco::SuperClusterCollection::const_iterator SC_iter = EESCHandle->begin();
	        SC_iter != EESCHandle->end();
	        SC_iter++) {
		if(fabs(SC_iter->eta()) < minEta_highEtaSC_) continue;
		AddMiniRecHitCollection(*SC_iter, reducedRecHit_EEmap, caloTopology, reducedRecHit_ESmap);

		const reco::SuperCluster& sc = *(SC_iter);
		reco::CaloCluster_iterator it = sc.clustersBegin();
		reco::CaloCluster_iterator itend = sc.clustersEnd();
		for ( ; it != itend; ++it) {
			reco::CaloCluster caloClus(**it);
			reducedCaloClusterCollection->push_back(caloClus);
		}
	}


	//------------------------------ fill the alcareco reduced recHit collection
	for(std::set<DetId>::const_iterator itr = reducedRecHit_EBmap.begin();
	        itr != reducedRecHit_EBmap.end(); itr++) {
		if (barrelHitsCollection->find(*itr) != barrelHitsCollection->end())
			miniEBRecHitCollection->push_back(*(barrelHitsCollection->find(*itr)));
		if (barrelUncalibHitsCollection != NULL && barrelUncalibHitsCollection->find(*itr) != barrelUncalibHitsCollection->end())
			miniEBUncalibRecHitCollection->push_back(*(barrelUncalibHitsCollection->find(*itr)));
	}

	for(std::set<DetId>::const_iterator itr = reducedRecHit_EEmap.begin();
	        itr != reducedRecHit_EEmap.end(); itr++) {
		if (endcapHitsCollection->find(*itr) != endcapHitsCollection->end())
			miniEERecHitCollection->push_back(*(endcapHitsCollection->find(*itr)));
		if (endcapUncalibHitsCollection != NULL && endcapUncalibHitsCollection->find(*itr) != endcapUncalibHitsCollection->end())
			miniEEUncalibRecHitCollection->push_back(*(endcapUncalibHitsCollection->find(*itr)));
	}

	for(std::set<DetId>::const_iterator itr = reducedRecHit_ESmap.begin();
	        itr != reducedRecHit_ESmap.end(); itr++) {
		if (preshowerHitsCollection->find(*itr) != preshowerHitsCollection->end())
			miniESRecHitCollection->push_back(*(preshowerHitsCollection->find(*itr)));
	}

	//--------------------------------------- Put selected information in the event
	iEvent.put( std::move( miniEBRecHitCollection ), alcaBarrelHitsCollection_ );
	iEvent.put( std::move( miniEBUncalibRecHitCollection ), alcaBarrelUncalibHitsCollection_ );
	iEvent.put( std::move( miniEERecHitCollection ), alcaEndcapHitsCollection_ );
	iEvent.put( std::move( miniEEUncalibRecHitCollection ), alcaEndcapUncalibHitsCollection_ );
	iEvent.put( std::move( miniESRecHitCollection ), alcaPreshowerHitsCollection_ );
	iEvent.put( std::move( reducedCaloClusterCollection ), alcaCaloClusterCollection_);
}

void AlCaECALRecHitReducer::AddMiniRecHitCollection(const reco::SuperCluster& sc,
        std::set<DetId>& reducedRecHitMap,
        const CaloTopology *caloTopology, std::set<DetId>& reducedESRecHitMap
                                                   )
{
	DetId seed = (sc.seed()->seed());
	int phiSize = phiSize_, etaSize = etaSize_;
	if(seed.subdetId() != EcalBarrel) { // if not EB, take a square window
		etaSize = std::max(phiSize_, etaSize_);
		phiSize = etaSize;
	}

	std::vector<DetId> recHit_window = caloTopology->getWindow(seed, phiSize, etaSize);
	for(unsigned int i = 0; i < recHit_window.size(); i++) {
		reducedRecHitMap.insert(recHit_window[i]);
	}

	const std::vector< std::pair<DetId, float> > & scHits = sc.hitsAndFractions();
	for(std::vector< std::pair<DetId, float> >::const_iterator scHit_itr = scHits.begin();
	        scHit_itr != scHits.end(); scHit_itr++) {
		// the map fills just one time (avoiding double insert of recHits)
		reducedRecHitMap.insert(scHit_itr->first);
	}

	if (sc.preshowerClusters().isAvailable()) {
//		std::cout << "[DEBUG] Preshower available!" << std::endl;
	  for( auto iES = sc.preshowerClustersBegin(); iES != sc.preshowerClustersEnd(); ++iES) {
			const std::vector< std::pair<DetId, float> >& hits = (*iES)->hitsAndFractions();
			for(std::vector<std::pair<DetId, float> >::const_iterator rh = hits.begin(); rh != hits.end(); ++rh) {
				reducedESRecHitMap.insert(rh->first);
			}
		}
	}


	return;
}


DEFINE_FWK_MODULE(AlCaECALRecHitReducer);


