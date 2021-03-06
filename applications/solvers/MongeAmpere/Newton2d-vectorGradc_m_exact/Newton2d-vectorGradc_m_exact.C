/*---------------------------------------------------------------------------*\
 =========                   |
 \\      /   F ield          | OpenFOAM: The Open Source CFD Toolbox
  \\    /    O peration      |
   \\  /     A nd            | Copyright (C) 1991-2007 OpenCFD Ltd.
    \\/      M anipulation   |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software; you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by the
    Free Software Foundation; either version 2 of the License, or (at your
    option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM; if not, write to the Free Software Foundation,
    Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

Application
    Newton2d-vectorGradc_m

Description
    Solves the Monge-Ampere equation to move a mesh based on a monitor
    function defined on the original mesh by using Newton's Method.
    The gradient of c/m is calculated as a vector on the rMesh and the full 
    vectory is transferred to the computational mesh before the divergence is
    taken. This appear to me to be the correct thing to do but it always slows
    convergence. Don't understand why

\*---------------------------------------------------------------------------*/

#include "HodgeOps.H"
#include "fvCFD.H"
#include "monitorFunction.H"
#include "faceToPointReconstruct.H"
#include "setInternalValues.H"

using namespace Foam;

// * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * //
// Main program:

int main(int argc, char *argv[])
{
    argList::noParallel();
    #include "setRootCase.H"
    #include "createTime.H"
    #include "createMesh.H"
    // Create the mesh to be moved
    fvMesh rMesh
    (
        Foam::IOobject
        (
            "rMesh", runTime.timeName(), runTime,
            IOobject::MUST_READ, IOobject::AUTO_WRITE
        )
    );
    HodgeOps H(rMesh);
    dimensionedScalar Vtot("Vtot", dimVol, gSum(mesh.V()));

    // Open control dictionary
    IOdictionary controlDict
    (
        IOobject
        (
            args.executable() + "Dict", runTime.system(), runTime,
            IOobject::MUST_READ_IF_MODIFIED
        )
    );
    
    // The monitor funciton
    autoPtr<monitorFunction> monitorFunc(monitorFunction::New(controlDict));
    
    // The under-relaxation factors for the differnet parts of the Newton
    // solver
    const dimensionedScalar Gamma1(controlDict.lookup("Gamma1"));
    const dimensionedScalar Gamma2(controlDict.lookup("Gamma2"));
    const scalar conv = readScalar(controlDict.lookup("conv"));
    const int nCorr = readLabel(mesh.solutionDict().lookup("nCorrectors"));
    const dimensionedScalar matrixRelax =
          readScalar(controlDict.lookup("matrixRelax"))
       * dimensionedScalar("", dimLength,mesh.bounds().span().y())/min(mesh.V());
    Info << "matrixRelax = " << matrixRelax << endl;
    const scalar gradientSmoothingCoeff = readScalar
    (
        controlDict.lookup("gradientSmoothingCoeff")
    );
    const label nSmooth = readLabel(controlDict.lookup("nSmooth"));

    #include "createFields.H"

    Info << "Iteration = " << runTime.timeName()
         << " PABe = " << PABe.value() << endl;

    // Use time-steps instead of iterations to solve the Monge-Ampere eqn
    bool converged = false;
    while (runTime.loop())
    {
        Info<< "Time = " << runTime.timeName() << endl;

        // Calculate the matrix: matrixA = 1+fvc::laplacian(phiBar)-Hessian
        phiBarLaplacian = fvc::laplacian(Phi);
        tensor localA (0,0,0,0,0,0,0,0,0);
        scalar localAew = 0.0;
        bool disp=true;
        forAll(matrixA, cellI)
        {
            
            matrixA[cellI] = diagTensor::one*(1+phiBarLaplacian[cellI]) - Hessian[cellI];
            matrixA[cellI].yy() = 1.0;
            localA = matrixA[cellI];
            localAew = eigenValues(localA)[0];
            if(localAew <= 0)
            {
                if(disp)
                {
                    Info << "Minimum eigenvalue = " << localAew << endl;
                    disp = false;
                }
                matrixA[cellI] = localA + (1.0e-5 - localAew)*diagTensor::one;
            }
            else
            {
                matrixA[cellI] = localA;
            }
        }

        // Calculate c/m and its gradients on both meshes
        c_m = equiDistMean/monitorNew;
        c_mR = equiDistMean/monitorR;

        // calculate the gradient of c_m in physical space (without compact
        // correction of the normal component)
        snGradc_mR = fvc::snGrad(c_mR);
        volVectorField gradc_mR_c("gradc_mR_c", fvc::grad(c_mR));

        // Transfer the cell centre gradient to the computational mesh, 
        // smooth then interpolate
        static_cast<DimensionedField<vector,volMesh> >(gradc_m_c.internalField())
             = gradc_mR_c.internalField();

        for(int i = 0; i < nSmooth; i++)
        {
            gradc_m_c += gradientSmoothingCoeff
                        *fvc::laplacian(sqr(1/mesh.deltaCoeffs()), gradc_m_c);
        }
        gradc_m = fvc::interpolate(gradc_m_c);

        //USING HodgeOps
        volVectorField reconstruct_snGradc_mR = H.reconstructd(snGradc_mR*H.magd());
        gradc_mR = fvc::interpolate(reconstruct_snGradc_mR);

        gradc_mR = equiDistMean*monitorFunc().grad(rMesh, gradc_mR);
        static_cast<DimensionedField<vector,surfaceMesh> >(gradc_m.internalField())
             = gradc_mR.internalField();

        // The divergence of sngradc_m
        surfaceScalarField flux = mesh.Sf() & gradc_m;
        lapc_m = fvc::div(flux);
        
        // Setup and solve the MA equation to find Phi(t+1) 
        solverPerformance sp;
        for (int iCorr = 0; iCorr < nCorr; iCorr++)
        {
            fvScalarMatrix PhiEqn
            (
                fvm::Sp(matrixRelax,phi)
              - Gamma1*fvm::laplacian(matrixA, phi)
              + Gamma2*fvm::div(flux, phi)
              - Gamma2*fvm::Sp(lapc_m, phi)
              - detHess + c_m
            );

            // Solve the matrix
            sp = PhiEqn.solve();
        }
        Phi += phi;
        phi == dimensionedScalar("phi", dimArea, scalar(0));

        // Calculate the gradient of Phi at cell centres and on faces
        gradPhi = fvc::grad(Phi);

        // Interpolate gradPhi (gradient of Phi) onto faces and correct the normal component
        gradPhif = fvc::interpolate(gradPhi);
        gradPhif += (fvc::snGrad(Phi) - (gradPhif & mesh.Sf())/mesh.magSf())
                    *mesh.Sf()/mesh.magSf();

        // Map gradPhi onto vertices in order to create the new mesh
        pointVectorField gradPhiP
             = fvc::faceToPointReconstruct(fvc::snGrad(Phi));
        rMesh.movePoints(mesh.points() + gradPhiP);

        // finite difference Hessian of phiBar and its determinant
        Hessian = fvc::grad(gradPhif);
        forAll(detHess, cellI)
        {
            detHess[cellI] = det(diagTensor::one + Hessian[cellI]);
        }

        // map to or calculate the monitor function on the new mesh
        monitorR = monitorFunc().map(rMesh, monitor);
        static_cast<DimensionedField<scalar,volMesh> >(monitorNew.internalField())
             = monitorR.internalField();
        monitorNew.correctBoundaryConditions();

        // The Equidistribution
        equiDist = monitorR*detHess;

        // mean equidistribution, c
        equiDistMean = fvc::domainIntegrate(detHess)
                       /fvc::domainIntegrate(1/monitorNew);

        // The global equidistribution as CV of equidistribution
        PABem = fvc::domainIntegrate(equiDist)/Vtot;
        PABe = sqrt(fvc::domainIntegrate(sqr(equiDist - PABem)))/(Vtot*PABem);
        converged = PABe.value() < conv; // || sp.nIterations() <= 0;

        Info << "Iteration = " << runTime.timeName()
             << " PABe = " << PABe.value() << endl;

        if (converged)
        {
            Info << "ExecutionTime = " << runTime.elapsedCpuTime() << " s"
                 << nl <<endl;
            runTime.writeAndEnd();
        }
        runTime.write();
    }
    
    Info << "End\n" << endl;

    return 0;
}


// ************************************************************************* //
