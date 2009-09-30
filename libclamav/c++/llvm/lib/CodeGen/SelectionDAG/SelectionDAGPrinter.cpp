//===-- SelectionDAGPrinter.cpp - Implement SelectionDAG::viewGraph() -----===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This implements the SelectionDAG::viewGraph method.
//
//===----------------------------------------------------------------------===//

#include "ScheduleDAGSDNodes.h"
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Assembly/Writer.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/MachineConstantPool.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineModuleInfo.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/Analysis/DebugInfo.h"
#include "llvm/Target/TargetRegisterInfo.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/GraphWriter.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/config.h"
#include <fstream>
using namespace llvm;

namespace llvm {
  template<>
  struct DOTGraphTraits<SelectionDAG*> : public DefaultDOTGraphTraits {
    static bool hasEdgeDestLabels() {
      return true;
    }

    static unsigned numEdgeDestLabels(const void *Node) {
      return ((const SDNode *) Node)->getNumValues();
    }

    static std::string getEdgeDestLabel(const void *Node, unsigned i) {
      return ((const SDNode *) Node)->getValueType(i).getEVTString();
    }

    /// edgeTargetsEdgeSource - This method returns true if this outgoing edge
    /// should actually target another edge source, not a node.  If this method is
    /// implemented, getEdgeTarget should be implemented.
    template<typename EdgeIter>
    static bool edgeTargetsEdgeSource(const void *Node, EdgeIter I) {
      return true;
    }

    /// getEdgeTarget - If edgeTargetsEdgeSource returns true, this method is
    /// called to determine which outgoing edge of Node is the target of this
    /// edge.
    template<typename EdgeIter>
    static EdgeIter getEdgeTarget(const void *Node, EdgeIter I) {
      SDNode *TargetNode = *I;
      SDNodeIterator NI = SDNodeIterator::begin(TargetNode);
      std::advance(NI, I.getNode()->getOperand(I.getOperand()).getResNo());
      return NI;
    }

    static std::string getGraphName(const SelectionDAG *G) {
      return G->getMachineFunction().getFunction()->getName();
    }

    static bool renderGraphFromBottomUp() {
      return true;
    }
    
    static bool hasNodeAddressLabel(const SDNode *Node,
                                    const SelectionDAG *Graph) {
      return true;
    }
    
    /// If you want to override the dot attributes printed for a particular
    /// edge, override this method.
    template<typename EdgeIter>
    static std::string getEdgeAttributes(const void *Node, EdgeIter EI) {
      SDValue Op = EI.getNode()->getOperand(EI.getOperand());
      EVT VT = Op.getValueType();
      if (VT == MVT::Flag)
        return "color=red,style=bold";
      else if (VT == MVT::Other)
        return "color=blue,style=dashed";
      return "";
    }
    

    static std::string getNodeLabel(const SDNode *Node,
                                    const SelectionDAG *Graph,
                                    bool ShortNames);
    static std::string getNodeAttributes(const SDNode *N,
                                         const SelectionDAG *Graph) {
#ifndef NDEBUG
      const std::string &Attrs = Graph->getGraphAttrs(N);
      if (!Attrs.empty()) {
        if (Attrs.find("shape=") == std::string::npos)
          return std::string("shape=Mrecord,") + Attrs;
        else
          return Attrs;
      }
#endif
      return "shape=Mrecord";
    }

    static void addCustomGraphFeatures(SelectionDAG *G,
                                       GraphWriter<SelectionDAG*> &GW) {
      GW.emitSimpleNode(0, "plaintext=circle", "GraphRoot");
      if (G->getRoot().getNode())
        GW.emitEdge(0, -1, G->getRoot().getNode(), G->getRoot().getResNo(),
                    "color=blue,style=dashed");
    }
  };
}

std::string DOTGraphTraits<SelectionDAG*>::getNodeLabel(const SDNode *Node,
                                                        const SelectionDAG *G,
                                                        bool ShortNames) {
  std::string Result = Node->getOperationName(G);
  {
    raw_string_ostream OS(Result);
    Node->print_details(OS, G);
  }
  return Result;
}


/// viewGraph - Pop up a ghostview window with the reachable parts of the DAG
/// rendered using 'dot'.
///
void SelectionDAG::viewGraph(const std::string &Title) {
// This code is only for debugging!
#ifndef NDEBUG
  ViewGraph(this, "dag." + getMachineFunction().getFunction()->getNameStr(), 
            false, Title);
#else
  errs() << "SelectionDAG::viewGraph is only available in debug builds on "
         << "systems with Graphviz or gv!\n";
#endif  // NDEBUG
}

// This overload is defined out-of-line here instead of just using a
// default parameter because this is easiest for gdb to call.
void SelectionDAG::viewGraph() {
  viewGraph("");
}

/// clearGraphAttrs - Clear all previously defined node graph attributes.
/// Intended to be used from a debugging tool (eg. gdb).
void SelectionDAG::clearGraphAttrs() {
#ifndef NDEBUG
  NodeGraphAttrs.clear();
#else
  errs() << "SelectionDAG::clearGraphAttrs is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
#endif
}


/// setGraphAttrs - Set graph attributes for a node. (eg. "color=red".)
///
void SelectionDAG::setGraphAttrs(const SDNode *N, const char *Attrs) {
#ifndef NDEBUG
  NodeGraphAttrs[N] = Attrs;
#else
  errs() << "SelectionDAG::setGraphAttrs is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
#endif
}


/// getGraphAttrs - Get graph attributes for a node. (eg. "color=red".)
/// Used from getNodeAttributes.
const std::string SelectionDAG::getGraphAttrs(const SDNode *N) const {
#ifndef NDEBUG
  std::map<const SDNode *, std::string>::const_iterator I =
    NodeGraphAttrs.find(N);
    
  if (I != NodeGraphAttrs.end())
    return I->second;
  else
    return "";
#else
  errs() << "SelectionDAG::getGraphAttrs is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
  return std::string("");
#endif
}

/// setGraphColor - Convenience for setting node color attribute.
///
void SelectionDAG::setGraphColor(const SDNode *N, const char *Color) {
#ifndef NDEBUG
  NodeGraphAttrs[N] = std::string("color=") + Color;
#else
  errs() << "SelectionDAG::setGraphColor is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
#endif
}

/// setSubgraphColorHelper - Implement setSubgraphColor.  Return
/// whether we truncated the search.
///
bool SelectionDAG::setSubgraphColorHelper(SDNode *N, const char *Color, DenseSet<SDNode *> &visited,
                                          int level, bool &printed) {
  bool hit_limit = false;

#ifndef NDEBUG
  if (level >= 20) {
    if (!printed) {
      printed = true;
      DEBUG(errs() << "setSubgraphColor hit max level\n");
    }
    return true;
  }

  unsigned oldSize = visited.size();
  visited.insert(N);
  if (visited.size() != oldSize) {
    setGraphColor(N, Color);
    for(SDNodeIterator i = SDNodeIterator::begin(N), iend = SDNodeIterator::end(N);
        i != iend;
        ++i) {
      hit_limit = setSubgraphColorHelper(*i, Color, visited, level+1, printed) || hit_limit;
    }
  }
#else
  errs() << "SelectionDAG::setSubgraphColor is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
#endif
  return hit_limit;
}

/// setSubgraphColor - Convenience for setting subgraph color attribute.
///
void SelectionDAG::setSubgraphColor(SDNode *N, const char *Color) {
#ifndef NDEBUG
  DenseSet<SDNode *> visited;
  bool printed = false;
  if (setSubgraphColorHelper(N, Color, visited, 0, printed)) {
    // Visually mark that we hit the limit
    if (strcmp(Color, "red") == 0) {
      setSubgraphColorHelper(N, "blue", visited, 0, printed);
    }
    else if (strcmp(Color, "yellow") == 0) {
      setSubgraphColorHelper(N, "green", visited, 0, printed);
    }
  }

#else
  errs() << "SelectionDAG::setSubgraphColor is only available in debug builds"
         << " on systems with Graphviz or gv!\n";
#endif
}

std::string ScheduleDAGSDNodes::getGraphNodeLabel(const SUnit *SU) const {
  std::string s;
  raw_string_ostream O(s);
  O << "SU(" << SU->NodeNum << "): ";
  if (SU->getNode()) {
    SmallVector<SDNode *, 4> FlaggedNodes;
    for (SDNode *N = SU->getNode(); N; N = N->getFlaggedNode())
      FlaggedNodes.push_back(N);
    while (!FlaggedNodes.empty()) {
      O << DOTGraphTraits<SelectionDAG*>::getNodeLabel(FlaggedNodes.back(),
                                                       DAG, false);
      FlaggedNodes.pop_back();
      if (!FlaggedNodes.empty())
        O << "\n    ";
    }
  } else {
    O << "CROSS RC COPY";
  }
  return O.str();
}

void ScheduleDAGSDNodes::getCustomGraphFeatures(GraphWriter<ScheduleDAG*> &GW) const {
  if (DAG) {
    // Draw a special "GraphRoot" node to indicate the root of the graph.
    GW.emitSimpleNode(0, "plaintext=circle", "GraphRoot");
    const SDNode *N = DAG->getRoot().getNode();
    if (N && N->getNodeId() != -1)
      GW.emitEdge(0, -1, &SUnits[N->getNodeId()], -1,
                  "color=blue,style=dashed");
  }
}