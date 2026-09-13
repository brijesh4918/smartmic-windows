/*++
    mintopo.h -- topology miniport.

    The topology filter is what makes Windows show a *microphone*, with a jack
    and a name, rather than an anonymous stream. It carries no audio.
--*/
#ifndef SMARTMIC_MINTOPO_H
#define SMARTMIC_MINTOPO_H

#include "common.h"

#define KSPIN_TOPO_MIC_ELEMENT  0   /* the "physical" microphone */
#define KSPIN_TOPO_BRIDGE       1   /* wired to the wave filter's bridge pin */

class CMiniportTopology : public IMiniportTopology,
                          public CUnknown
{
public:
    DECLARE_STD_UNKNOWN();
    DEFINE_STD_CONSTRUCTOR(CMiniportTopology);
    ~CMiniportTopology();

    IMP_IMiniportTopology;

private:
    PPORTTOPOLOGY m_port;
};

#endif
