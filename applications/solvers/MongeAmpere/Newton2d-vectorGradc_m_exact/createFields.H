// Create the mesh displacement potential, Phi
volScalarField Phi
(
    IOobject("Phi",runTime.timeName(),mesh,
             IOobject::MUST_READ, IOobject::AUTO_WRITE),
    mesh
);
Phi.oldTime();

// Create the perturbation from Phi
volScalarField phi("phi", Phi);
phi == dimensionedScalar("phi", dimArea, scalar(0));

// Create phiBarLaplacian
volScalarField phiBarLaplacian("phiBarLaplacian", fvc::laplacian(Phi));

// The gradient of Phi at cell centres
volVectorField gradPhi
(
    IOobject("gradPhi", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::reconstruct(fvc::snGrad(Phi)*mesh.magSf())
);
static_cast<GeometricField<Vector<double>, fvPatchField, volMesh>::Boundary>
(
    gradPhi.boundaryField()
)
== (static_cast<volVectorField>(fvc::grad(Phi))).boundaryField();

// The gradient of Phi on faces
surfaceVectorField gradPhif
(
    IOobject("gradPhif", runTime.timeName(), mesh, 
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::interpolate(gradPhi),
    "calculated"
);
gradPhif += (fvc::snGrad(Phi) - (gradPhif & mesh.Sf())/mesh.magSf())
            *mesh.Sf()/mesh.magSf();

// Hessian of Phi and its determinant
volTensorField Hessian("Hessian", fvc::grad(gradPhif));
volScalarField detHess
(
    IOobject("detHess", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    mesh,
    dimensionedScalar("", dimless, scalar(1))
);

// The reqired matrix A
volTensorField matrixA
(
    IOobject("matrixA", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    mesh,
    dimensionedTensor("I", dimless, tensor::one)
);
volScalarField detHessR
(
    IOobject("detHessR", runTime.timeName(), rMesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    rMesh,
    dimensionedScalar("", dimless, scalar(1))
);

// The original monitor function on the old mesh
const volScalarField monitor
(
    IOobject("monitor", runTime.timeName(), mesh, IOobject::READ_IF_PRESENT),
    mesh,
    dimensionedScalar("monitor", dimensionSet(0,0,0,0,0), scalar(1)),
    wordList(5,"zeroGradient")
);

// The monitor function on the old mesh calculated from the new mesh
volScalarField monitorNew("monitorNew", monitor);

// The monitor function on the new mesh
volScalarField monitorR
(
    IOobject("monitorR", runTime.timeName(), rMesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    monitorFunc().map(rMesh, monitor)
);
static_cast<DimensionedField<scalar,volMesh> >(monitorNew.internalField())
     = monitorR.internalField();

// The mean equidistribution, c
dimensionedScalar equiDistMean = fvc::domainIntegrate(detHess)
                                 /fvc::domainIntegrate(1/monitorNew);

dimensionedScalar equiDistMeanR = fvc::domainIntegrate(detHessR)
                                 /fvc::domainIntegrate(1/monitorR);

equiDistMeanR = equiDistMean;

// The equidistribution
volScalarField equiDist
(
    IOobject("equiDist", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    monitorR*detHess
);

// The global equidistribution
dimensionedScalar PABem = fvc::domainIntegrate(equiDist)/Vtot;
dimensionedScalar PABe = sqrt(fvc::domainIntegrate(sqr(equiDist - PABem)))
                         /(Vtot*PABem);

// constant divided by monitor function:
volScalarField c_m
(
    IOobject("c_m", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    equiDistMean/monitorNew
);

// constant divided by monitor function:
volScalarField c_mR
(
    IOobject("c_mR", runTime.timeName(), rMesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    equiDistMeanR/monitorR
);

// The normal gradient of c_m on the physical mesh
surfaceScalarField snGradc_mR
(
    IOobject("snGradc_mR", runTime.timeName(), rMesh),
    fvc::snGrad(c_mR)
);

// Cell centred gradc_m on the computational mesh
volVectorField gradc_m_c
(
    IOobject("gradc_m_c", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::grad(c_m)
);

// The gradient of c_m on on the computational mesh
surfaceVectorField gradc_m
(
    IOobject("gradc_m", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::interpolate(gradc_m_c)
);

// The gradient of c_m on on the computational mesh computed by reconstruction
surfaceVectorField gradc_m_new
(
    IOobject("gradc_m_new", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::interpolate(gradc_m_c)
);
// The gradient of c_m on the physical mesh
surfaceVectorField gradc_mR
(
 IOobject("gradc_mR", runTime.timeName(), rMesh,
          IOobject::NO_READ, IOobject::AUTO_WRITE),
    fvc::interpolate(fvc::grad(c_mR))
);

// The divergence of gradc_m on the computational mesh
volScalarField lapc_m
(
    IOobject("lapc_m", runTime.timeName(), mesh,
             IOobject::NO_READ, IOobject::AUTO_WRITE
             ),
    fvc::div(mesh.Sf() & gradc_m)
);

