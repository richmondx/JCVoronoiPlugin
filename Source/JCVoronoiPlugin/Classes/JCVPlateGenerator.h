// 

#pragma once

#include "JCVTypes.h"
#include "JCVValueGenerator.h"

class FJCVPlateGenerator
{

    typedef FJCVValueGenerator::FRadialFill FRadialFill;

public:

    struct FOrogenParams
    {
        FRadialFill ValueParams;
        float OriginValueThreshold = .1f;
        bool bDivergentAsConvergent = false;

        FORCEINLINE FOrogenParams() = default;

        FORCEINLINE FOrogenParams(FRadialFill v, float o, bool d)
            : ValueParams(v)
            , OriginValueThreshold(o)
            , bDivergentAsConvergent(d)
        {
        }

        FORCEINLINE void Set(FRadialFill v, float o, bool d)
        {
            ValueParams = v;
            OriginValueThreshold = o;
            bDivergentAsConvergent = d;
        }
    };

    struct FTectonicPlate
    {
        const FJCVFeatureGroup& Feature;
        FVector2D Direction;
        float Magnitude;

        FORCEINLINE FTectonicPlate(const FJCVFeatureGroup& fg, const FVector2D& dir, float mag)
            : Feature(fg)
            , Direction(dir)
            , Magnitude(mag)
        {
        }

    private:
        FTectonicPlate() = default;
    };

    typedef TArray<FTectonicPlate> FTectonicGroup;

    static void GenerateOrogeny(
        FJCVIsland& PlateIsland,
        FJCVIsland& Landscape,
        const FTectonicGroup& Plates,
        const FOrogenParams& OrogenParams,
        FRandomStream& Rand
    ) {
        TMap<uint8, const FTectonicPlate*> plateMap;
        // Maps feature type to tectonic plates
        for (const FTectonicPlate& plate : Plates)
        {
            const FJCVFeatureGroup& fg( plate.Feature );
            const uint8 ft = fg.FeatureType;
            if (fg.HasCells() && ! plateMap.Contains(fg.FeatureType))
                plateMap.Emplace(ft, &plate);
        }

        FJCVGraphSet incidentEdges;
        // Find incident edges between plates
        incidentEdges.Reserve(PlateIsland.Num());
        for (TPair<uint8, const FTectonicPlate*> kv : plateMap)
        {
            check(kv.Value);
            const FTectonicPlate& plate( *kv.Value );
            const FJCVFeatureGroup& fg( plate.Feature );
            const uint8 ft0 = fg.FeatureType;
            for (uint8 ft1 : fg.Neighbours)
            {
                FJCVCellGroup cg;
                PlateIsland.GetBorderCells(cg, ft0, ft1);

                FJCVGraphSet es;
                es.Reserve(cg.Num());
                PlateIsland.GetBorderEdges(cg, es, ft1);

                incidentEdges.Append(es);
            }
        }
        incidentEdges.Shrink();

        FRadialFill nodeParams( OrogenParams.ValueParams );
        const float originThreshold = OrogenParams.OriginValueThreshold;
        const bool bDivergentAsConvergent = OrogenParams.bDivergentAsConvergent;
        // Generates orogen values on Landscape island parameter
        for (const FJCVGraphEdge* g : incidentEdges)
        {
            // Ensure valid involved incident cells
            const FJCVCell* c0( PlateIsland.Cell(g->edge->sites[0]) );
            const FJCVCell* c1( PlateIsland.Cell(g->edge->sites[1]) );
            if (! c0 || ! c1)
                continue;
            // Ensure valid involved incident plates
            const FTectonicPlate** pPlate0( plateMap.Find(c0->FeatureType) );
            const FTectonicPlate** pPlate1( plateMap.Find(c1->FeatureType) );
            if (! pPlate0 || ! pPlate1 || pPlate0 == pPlate1)
                continue;
            const FTectonicPlate& plate0( **pPlate0 );
            const FTectonicPlate& plate1( **pPlate1 );
            // Ensure valid incident origin cell
            const FJCVGraphNode node(g, 0);
            const FVector2D nodePos( node.vmid() );
            FJCVCell* cell( Landscape.Cell(Landscape->Find(nodePos)) );
            if (! cell)
                continue;
            // Make sure to only account convergent plate movement
            {
                const FVector2D dir0 = (nodePos - c0->V2D()).GetSafeNormal();
                const FVector2D dir1 = (nodePos - c1->V2D()).GetSafeNormal();
                const float dot0 = dir0 | plate0.Direction;
                const float dot1 = dir1 | plate1.Direction;
                if (bDivergentAsConvergent)
                {
                    if (    (dot0 < 0.f && dot1 < 0.f)
                        ||  (dot0 < 0.f && dot1 > 0.f)
                        ||  (dot0 < 0.f && dot1 > 0.f))
                        continue;
                }
                else if (dot0 < 0.f || dot1 < 0.f)
                    continue;
            }
            const float plateDot = FMath::Abs(plate0.Direction|plate1.Direction);
            const float plateMag = FMath::Abs(plate1.Magnitude-plate0.Magnitude);
            const float plateSpr = plateDot*plateMag;
            if (plateSpr < originThreshold)
                continue;
            nodeParams.Value = plateSpr;
            FJCVValueGenerator::AddRadialFill(Landscape, *cell, nodeParams, Rand);
        }
    }

    FORCEINLINE static void GenerateOrogeny(
        FJCVIsland& PlateIsland,
        FJCVIsland& Landscape,
        const FOrogenParams& OrogenParams,
        FRandomStream& Rand
    ) {
        FTectonicGroup plates( GenerateTectonicPlates(PlateIsland, Rand) );
        GenerateOrogeny(PlateIsland, Landscape, plates, OrogenParams, Rand);
    }

    static FTectonicGroup GenerateTectonicPlates(const TArray<const FJCVFeatureGroup*>& FeatureGroups, FRandomStream& Rand)
    {
        TArray<FTectonicPlate> tectonics;
        for (const FJCVFeatureGroup* fg : FeatureGroups)
        {
            if (fg && fg->HasCells())
            {
                const uint8 ft = fg->FeatureType;
                const FRotator randRot(0.f,Rand.GetFraction()*360.f,0.f);
                const FVector2D moveDir( randRot.Vector() );
                const float moveMag( Rand.GetFraction() );
                tectonics.Emplace(*fg, moveDir, moveMag);
            }
        }
        return tectonics;
    }

    FORCEINLINE static FTectonicGroup GenerateTectonicPlates(const FJCVIsland& Island, FRandomStream& Rand)
    {
        TArray<const FJCVFeatureGroup*> featureGroups;
        for (int32 i=0; i<Island.GetFeatureCount(); ++i)
            featureGroups.Emplace(Island.GetFeatureGroup(i));
        return GenerateTectonicPlates(featureGroups, Rand);
    }

};
