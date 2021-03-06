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
    PMA2D

Description
    Solves the Monge-Ampere equation to move a mesh based on a monitor
    function defined on the original mesh using the PMA method

\*---------------------------------------------------------------------------*/

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
    
    const dimensionedScalar Gamma(controlDict.lookup("Gamma"));
    const scalar conv = readScalar(controlDict.lookup("conv"));
    const dimensionedScalar dimfix(controlDict.lookup("dimfix"));

    #include "createFields.H"

    Info << "Iteration = " << runTime.timeName()
         << " PABe = " << PABe.value() << endl;

    // Use time-steps instead of iterations to solve the Monge-Ampere eqn
    bool converged = false;
    while (runTime.loop())
    {
        constant = fvc::domainIntegrate(pow(detHess*monitorNew,0.5))/Vtot;
        
        // Calculate the source terms for the PMA equation
        source = (pow(detHess*monitorNew, 0.5) - constant);

        // Setup and solve the PMA eqn
        fvScalarMatrix PhiEqn
        (
            dimfix*fvm::ddt(Phi)
            - Gamma*rdt*fvm::laplacian(Phi)
            + Gamma*rdt*fvc::laplacian(Phi)
            - source
        );
        solverPerformance sp = PhiEqn.solve();
        converged = sp.nIterations() <= 1;

        iteration++;
        
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

        if (min(detHess.internalField()).value() < 0)
        {
            Info << "PMA method has tangled" << endl;
            runTime.writeAndEnd();
        }

        // map to or calculate the monitor function on the new mesh
        monitorR = monitorFunc().map(rMesh, monitor);
        setInternalValues(monitorNew, monitorR);

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
