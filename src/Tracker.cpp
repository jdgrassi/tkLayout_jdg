#include "Tracker.h"

std::pair<double, double> Tracker::computeMinMaxEta() const {
  double min = std::numeric_limits<double>::max(), max = 0;
  for (auto m : modules()) {
    min = MIN(min, m->minEta());
    max = MAX(max, m->maxEta());
  } 

  //return std::make_pair(-1*log(tan(min/2.)), -1*log(tan(max/2.)));
  //return std::make_pair(min, max);
  return std::make_pair(-4.0,4.0); // CUIDADO to make it equal to the extended pixel - make it better ASAP!!
}

void Tracker::build() {
  try {
    check();

    double barrelMaxZ = 0;

    for (auto& mapel : barrelNode) {
      if (!containsOnly.empty() && containsOnly.count(mapel.first) == 0) continue;
      Barrel* b = GeometryFactory::make<Barrel>();
      b->myid(mapel.first);
      b->store(propertyTree());
      b->store(mapel.second);
      b->build();
      b->cutAtEta(etaCut());
      barrelMaxZ = MAX(b->maxZ(), barrelMaxZ);
      barrels_.push_back(b);
    }

    for (auto& mapel : endcapNode) {
      if (!containsOnly.empty() && containsOnly.count(mapel.first) == 0) continue;
      Endcap* e = GeometryFactory::make<Endcap>();
      e->myid(mapel.first);
      e->barrelMaxZ(barrelMaxZ);
      e->store(propertyTree());
      e->store(mapel.second);
      e->build();
      e->cutAtEta(etaCut());
      endcaps_.push_back(e);
    }

    class ModuleRemover : public GeometryVisitor {
      void visit(RodPair& m_rodPair) { m_rodPair.removeModules(); }
      void visit(Ring& m_ring) { m_ring.removeModules(); }
    };
    ModuleRemover m_moduleRemover;
    accept(m_moduleRemover);

    // Build support structures within tracker
    for (auto& mapel : supportNode) {
      SupportStructure* s = new SupportStructure();
      s->store(propertyTree());
      s->store(mapel.second);
      s->buildInTracker();
      supportStructures_.push_back(s);
    }
  }
  catch (PathfulException& pe) { pe.pushPath(fullid(*this)); throw; }

  accept(moduleSetVisitor_);

  class HierarchicalNameVisitor : public GeometryVisitor {
    int cntId = 0;
    string cnt;
    int c1, c2;
  public:
    void visit(Barrel& b) { cnt = b.myid(); cntId++; }
    void visit(Endcap& e) { cnt = e.myid(); cntId++; }
    void visit(Layer& l)  { c1 = l.myid(); }
    void visit(Disk& d)   { c1 = d.myid(); }
    void visit(RodPair& r){ c2 = r.myid(); }
    void visit(Ring& r)   { c2 = r.myid(); }
    void visit(Module& m) { m.cntNameId(cnt, cntId); }
    void visit(BarrelModule& m) { m.layer(c1); m.rod(c2); }
    void visit(EndcapModule& m) { m.disk(c1); m.ring(c2); }
  } cntNameVisitor;

  accept(cntNameVisitor);


  std::vector< std::pair<const Layer*, std::string> > sortedLayers;
  std::string barrelId;
  for (auto& b : barrels_) {
    barrelId = b.myid();
    for (const auto& l : b.layers()) { sortedLayers.push_back(std::make_pair(&l, barrelId)); }
  }
  std::sort(sortedLayers.begin(), sortedLayers.end(), [&] (std::pair<const Layer*, std::string> l1, std::pair<const Layer*, std::string> l2) { return l1.first->minR() < l2.first->minR(); });
  std::map< std::pair<std::string, int>, int > sortedLayersIds;
  int i = 1;
  for (const auto& l : sortedLayers) { sortedLayersIds.insert(std::make_pair(std::make_pair(l.second, l.first->myid()), i)); i++; }

  std::vector< std::pair<const Disk*, std::string> > sortedDisks;
  std::string endcapId;
  for (auto& e : endcaps_) {
    endcapId = e.myid();
    for (const auto& d : e.disks()) { sortedDisks.push_back(std::make_pair(&d, endcapId)); }
  }
  std::sort(sortedDisks.begin(), sortedDisks.end(), [&] (std::pair<const Disk*, std::string> d1, std::pair<const Disk*, std::string> d2) { return d1.first->averageZ() < d2.first->averageZ(); });
  std::map< std::pair<std::string, int>, int > sortedDisksIds;
  i = 1;
  for (const auto& d : sortedDisks) { sortedDisksIds.insert(std::make_pair(std::make_pair(d.second, d.first->myid()), i)); i++; }





  class BarrelDetIdBuilder : public SensorGeometryVisitor {
  private:
    std::string schemeName;
    std::vector<int> schemeShifts;
    std::map< std::pair<std::string, int>, int > sortedLayersIds;

    std::map<int, uint32_t> detIdRefs;

    std::string barrelName;
    int numRods;
    int numFlatRings;
    int numRings;

  public:
    BarrelDetIdBuilder(std::string name, std::vector<int> shifts, std::map< std::pair<std::string, int>, int > layersIds) : schemeName(name), schemeShifts(shifts), sortedLayersIds(layersIds) {}

    void visit(Barrel& b) {
      barrelName = b.myid();

      detIdRefs.insert(std::make_pair(0, 1));

      detIdRefs.insert(std::make_pair(1, 205 % 100));
			   
      detIdRefs.insert(std::make_pair(2, 0));
    }

    void visit(Layer& l) {
      std::pair<std::string, int> layerId;
      layerId = std::make_pair(barrelName, l.myid());
      l.layerNumber(sortedLayersIds.at(layerId));

      detIdRefs.insert(std::make_pair(3, l.layerNumber()));

      numRods = l.numRods();
      numFlatRings = l.buildNumModulesFlat();
      numRings = l.buildNumModules();
    }

    void visit(RodPair& r) {
      uint32_t phiRef = 1 + (uint32_t)(femod(r.Phi(), 2*M_PI) / (2*M_PI)) * numRods;
      detIdRefs.insert(std::make_pair(5, phiRef));
    }

    void visit(BarrelModule& m) {
      bool side = (m.minZ() > 0 ? 1 : 0);
      uint32_t ringRef;

      if (!m.isTilted()) {
	detIdRefs.insert(std::make_pair(4, 3));

	ringRef = (side ? m.uniRef().ring : 1 + m.uniRef().ring - numFlatRings);
      }

      else {
	uint32_t category = (side == 1 ? 2 : 1);
	detIdRefs.insert(std::make_pair(4, category));

	ringRef = (side ? m.uniRef().ring : 1 + m.uniRef().ring - numRings);
      }
      detIdRefs.insert(std::make_pair(6, ringRef));
    }

    void visit(Sensor& s) {
      uint32_t sensorRef = (s.innerOuter() == SensorPosition::LOWER ? 1 : 2);
      if (s.subdet() == ModuleSubdetector::BARREL) {
	detIdRefs.insert(std::make_pair(7, sensorRef));
	s.buildDetId(detIdRefs, schemeShifts);
      }  
    }

  };


  class EndcapDetIdBuilder : public SensorGeometryVisitor {
  private:
    std::string schemeName;
    std::vector<int> schemeShifts;
    std::map< std::pair<std::string, int>, int > sortedDisksIds;

    std::map<int, uint32_t> detIdRefs;

    std::string endcapName;
    int numEmptyRings;
    int numModules;

  public:
    EndcapDetIdBuilder(std::string barrelScheme, std::vector<int> shifts, std::map< std::pair<std::string, int>, int > disksIds) : schemeName(name), schemeShifts(shifts), sortedDisksIds(disksIds) {}

    void visit(Endcap& e) {
      detIdRefs.insert(std::make_pair(0, 1));

      detIdRefs.insert(std::make_pair(1, 204 % 100));
    }

    void visit(Disk& d) {
      std::pair<std::string, int> diskId;
      diskId = std::make_pair(endcapName, d.myid());
      d.diskNumber(sortedDisksIds.at(diskId));

      bool side = (d.minZ() > 0);

      uint32_t sideRef = (side ? 2 : 1);
      detIdRefs.insert(std::make_pair(2, sideRef));

      detIdRefs.insert(std::make_pair(3, 0));

      uint32_t diskRef = d.diskNumber();
      detIdRefs.insert(std::make_pair(4, diskRef));

      numEmptyRings = d.numEmptyRings();
    }
   
    void visit(Ring& r) {
      uint32_t ringRef = r.myid() - numEmptyRings;
      detIdRefs.insert(std::make_pair(5, ringRef));

      detIdRefs.insert(std::make_pair(6, 1));

      numModules = r.numModules();
    }
 
    void visit(EndcapModule& m) {
      uint32_t phiRef = 1 + (uint32_t)(femod(m.Phi(), 2*M_PI) / (2*M_PI)) * numModules;
      detIdRefs.insert(std::make_pair(7, phiRef));
    }

    void visit(Sensor& s) {
      uint32_t sensorRef = (s.innerOuter() == SensorPosition::LOWER ? 1 : 2);
      if (s.subdet() == ModuleSubdetector::ENDCAP) {
	detIdRefs.insert(std::make_pair(8, sensorRef));
	s.buildDetId(detIdRefs, schemeShifts);
      }
    }

  };


  if (!isPixelTracker()) {
    if (barrelDetIdScheme() == "Phase2Subdetector5" && detIdSchemes_.count("Phase2Subdetector5") != 0) {
      BarrelDetIdBuilder v(barrelDetIdScheme(), detIdSchemes_["Phase2Subdetector5"], sortedLayersIds);
      accept(v);
    }
    else logWARNING("barrelDetIdScheme = " + barrelDetIdScheme() + ". This barrel detId scheme is empty or incorrect or not currently implemented within tkLayout. No detId for Barrel sensors will be calculated.");

    if (endcapDetIdScheme() == "Phase2Subdetector4" && detIdSchemes_.count("Phase2Subdetector4") != 0) {
      EndcapDetIdBuilder v(endcapDetIdScheme(), detIdSchemes_["Phase2Subdetector4"], sortedDisksIds);
      accept(v);
    }
    else logWARNING("endcapDetIdScheme = " + endcapDetIdScheme() + ". This endcap detId scheme is empty or incorrect or not currently implemented within tkLayout. No detId for Endcap sensors will be calculated.");
  }


  cleanup();
  builtok(true);
}


std::map<std::string, std::vector<int> > Layer::detIdSchemes() {
  std::map<std::string, std::vector<int> > schemes;

  std::ifstream schemesStream(mainConfigHandler::instance().getDetIdSchemesDirectory() + "/" + insur::default_detidschemesfile);

  if (schemesStream.good()) {
    std::string line;
    while(getline(schemesStream, line).good()) {
      if (line.empty()) continue;
      auto schemeData = split(line, " ");
      std::string schemeName = schemeData.at(0);
      if (schemeData.size() < 2) logWARNING("DetId scheme " + schemeName + " : no data was entered." );
      else {
	std::vector<int> detIdShifts;
	int sum;
	for (int i = 1; i < schemeData.size(); i++) {
	  int shift = str2any<int>(schemeData.at(i));
	  detIdShifts.push_back(shift);
	  sum += shift; 
	}
	if (sum != 32) logWARNING("DetId scheme " + schemeName + " : The sum of Det Id shifts is not equal to 32." );
	else schemes.insert(std::make_pair(schemeName, detIdShifts));
      }
    }
    schemesStream.close();
  } else logWARNING("No file defining a detId scheme has been found.");

  return schemes;
}
