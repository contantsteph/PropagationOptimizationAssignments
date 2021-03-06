/*    Copyright (c) 2010-2018, Delft University of Technology
 *    All rigths reserved
 *
 *    This file is part of the Tudat. Redistribution and use in source and
 *    binary forms, with or without modification, are permitted exclusively
 *    under the terms of the Modified BSD license. You should have received
 *    a copy of the license with this file. If not, please or visit:
 *    http://tudat.tudelft.nl/LICENSE.
 */

#include <chrono>

#include <Tudat/SimulationSetup/tudatSimulationHeader.h>
#include <Tudat/Astrodynamics/TrajectoryDesign/trajectory.h>
#include <Tudat/SimulationSetup/PropagationSetup/propagationPatchedConicFullProblem.h>
#include <Tudat/SimulationSetup/PropagationSetup/propagationLambertTargeterFullProblem.h>

#include "../applicationOutput.h"

using namespace tudat;
using namespace tudat::simulation_setup;
using namespace tudat::propagators;
using namespace tudat::numerical_integrators;
using namespace tudat::orbital_element_conversions;
using namespace tudat::basic_mathematics;
using namespace tudat::gravitation;
using namespace tudat::numerical_integrators;
using namespace tudat::transfer_trajectories;

//! Function to directly setup a vector of acceleration maps for a patched conics trajectory.
std::vector < basic_astrodynamics::AccelerationMap > getAccelerationModelsPerturbedPatchedConicsTrajectory(
        const double numberOfLegs,
        const std::string& nameCentralBody,
        const std::string& nameBodyToPropagate,
        const simulation_setup::NamedBodyMap& bodyMap,
        const std::vector< std::string >& transferBodyOrder )
{
    std::vector< basic_astrodynamics::AccelerationMap > accelerationMapsVector;
    for (int i = 0 ; i < numberOfLegs ; i++)
    {
        SelectedAccelerationMap accelerationSettingsMap;

        accelerationSettingsMap[ nameBodyToPropagate ][ nameCentralBody ].push_back(
                    std::make_shared< simulation_setup::AccelerationSettings >(
                        basic_astrodynamics::central_gravity ) );
        accelerationSettingsMap[ nameBodyToPropagate ][ transferBodyOrder.at( i ) ].push_back(
                    std::make_shared< AccelerationSettings >( basic_astrodynamics::point_mass_gravity ) );

        if( i != numberOfLegs -1 )
        {
            if( transferBodyOrder.at( i ) != transferBodyOrder.at( i + 1 ) )
            {
                accelerationSettingsMap[ nameBodyToPropagate ][ transferBodyOrder.at( i + 1 ) ].push_back(
                            std::make_shared< AccelerationSettings >( basic_astrodynamics::point_mass_gravity ) );
            }
        }

        accelerationMapsVector.push_back( createAccelerationModelsMap(
                                              bodyMap, accelerationSettingsMap, { nameBodyToPropagate }, { nameCentralBody } ) );
    }

    return accelerationMapsVector;

}

/*!
 *   This function computes a patched conic trajectory with a given set of flyby bodies, minimum periapsis distances. The
 *   order of bodies is defined as Earth-Venus-X-Y-Jupiter, with X and Y user-defined. A Trajectory object is created that
 *   computes the required Delta V at arrival and at each flyby (no DSMs are considered). The departure Delta V is not included
 *   in the final Delta V
 *
 *   Subsequently, the function fullPropagationPatchedConicsTrajectory is called, which propagates the problem
 *   numerically in the following way:
 *
 *   - The patched conic (consisting of a set of Lambert targeters), is reconstructed, and the output (vehicle state) at regular
 *     intervals is saved.
 *   - Starting at the midpoint (in time) of each transfer leg, the spacecraft state is numerically propagated using acceleration/
 *     environment settings that may be defined by the user. The propagation is done from the midpoint forwards, and backwards,
 *     to obtain the state history of the full leg.
 *
 *   In the code, as given to you, the dynamical model used in the numerical propagation is not fully consistent with that of the
 *   patched conic method. In addition to the point mass gravity attraction by the Sun (Sun fixed at the origin), the perturbations
 *   of the arrival and departure planet are also taken into account (as point-mass gravities)
 *
 *   Key outputs (per leg):
 *
 *   lambertTargeterResultForEachLeg: a list of the state history of the spacecraft (per leg) according to the patched conic
 *      method
 *   fullProblemResultForEachLeg: a list of the state history of the spacecraft (per leg) as produced by the numerical propagation
 *
 *   Input parameters:
 *
 *   trajectoryIndependentVariables: A vector defining the start time (first entry, in seconds since J2000) and duration of all 4
 *      legs (second, third, fourth and fifth entries respectively). The final entry
 *      of this vector is an integer defining which entry of the transferCaseNames vector is to be used,
 *      e.g. which bodies X and Y are used for the 3rd and 4th body.
 */
int main( )
{
    // Load Spice kernels.
    spice_interface::loadStandardSpiceKernels( );

    std::string outputPath = tudat_applications::getOutputPath( "HighThrust" );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////        TRANSFER SETTINGS                 //////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Define different cases for 3rd and 4th body
    std::vector< std::string > transferCases = { "EVEEJ", "EVVEJ",  "EVEVJ", "EVVMJ", "EVEMJ", "EVMMJ", "EVMVJ"  };
    std::vector< std::pair< std::string, std::string > > transferCaseNames =
    { { "Earth", "Earth" }, { "Venus", "Earth" }, { "Earth", "Venus" }, { "Venus", "Mars" }, { "Earth", "Mars" },
      { "Mars", "Mars" }, { "Mars", "Venus" } };

    // DEFINE PROBLEM INDEPENDENT VARIABLES HERE:
    std::vector< double > trajectoryParameters =
    { -1851.46422926478,	 94.13188652993128,	 381.9429079287791,	 55.6729929900098,	 700.990295462437 , 1 };

    int transferCase = trajectoryParameters.at( 5 );

    // Set body order (no DSM) for current settings
    std::vector< std::string > transferBodyOrder =
    { "Earth", "Venus", transferCaseNames.at( transferCase ).first, transferCaseNames.at( transferCase ).second, "Jupiter" };
    std::vector< TransferLegType > transferLegTypes = { mga_Departure, mga_Swingby, mga_Swingby, mga_Swingby, capture };

    // Define settings for capture at target planet
    double captureSemiMajorAxis = 1.0895e8 / 0.02;
    double captureEccentricity = 0.98;
    std::vector< double > departureCaptureSemiMajorAxes = { TUDAT_NAN, captureSemiMajorAxis };
    std::vector< double > departureCaptureEccentricities = { TUDAT_NAN, captureEccentricity };
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////        SETUP SOLAR SYSTEM BODIES            ///////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Create body map
    NamedBodyMap bodyMapForPatchedConic = setupBodyMapFromEphemeridesForPatchedConicsTrajectory(
                "Sun", "Spacecraft", transferBodyOrder );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ///////////////////////             CREATE SPACECRAFT            //////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Create spacecraft object.
    bodyMapForPatchedConic[ "Spacecraft" ] = std::make_shared< simulation_setup::Body >( );
    bodyMapForPatchedConic[ "Spacecraft" ]->setConstantBodyMass( 400.0 );

    // Finalize body creation.
    setGlobalFrameBodyEphemerides( bodyMapForPatchedConic, "SSB", "ECLIPJ2000" );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////             CREATE PATCHED CONIC SEMI-ANALYTICAL TRAJECTORY            ///////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Get list of minimum flyby periapsis radii
    std::vector< double > minimumPericenterRadii = getDefaultMinimumPericenterRadii(
                transferBodyOrder );

    std::vector< double > trajectoryIndependentVariables;
    for( unsigned int i = 0; i < trajectoryParameters.size( ) - 1; i++ )
    {
        trajectoryIndependentVariables.push_back( trajectoryParameters.at( i ) * physical_constants::JULIAN_DAY );
    }

    // Add entry for interface consistency
    trajectoryIndependentVariables.push_back( TUDAT_NAN );

    // Create patched conic calculation object (no numerical propagation; departure Delta V not included)
    transfer_trajectories::Trajectory trajectory = createTransferTrajectoryObject(
                bodyMapForPatchedConic, transferBodyOrder, "Sun", transferLegTypes, trajectoryIndependentVariables,
                minimumPericenterRadii, false, TUDAT_NAN, TUDAT_NAN, true,
                captureSemiMajorAxis, captureEccentricity );


    // Retrieve total Delta V values
    double totalDeltaV;
    trajectory.calculateTrajectory( totalDeltaV );
    double captureDeltaV;
    trajectory.getCaptureDeltaV( captureDeltaV );

    std::cout<<"Total/capture Delta V: "<<totalDeltaV<<" "<<captureDeltaV<<std::endl;

    // Retrieve times, positions, and delta V at each maneuver
    std::vector < Eigen::Vector3d > positionVector;
    std::vector < double > timeVector;
    std::vector < double > deltaVVector;
    trajectory.maneuvers( positionVector, timeVector, deltaVVector );

    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    ////////////////             NUMERICALLY PROPAGATE DYNAMICS            ////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

    // Define environment for propagation (equal to that of patched conic)
    NamedBodyMap bodyMapForPropagation = bodyMapForPatchedConic;

    // Define acceleration settings
    std::vector< basic_astrodynamics::AccelerationMap > accelerationMap =
            getAccelerationModelsPerturbedPatchedConicsTrajectory(
                transferLegTypes.size( ), "Sun", "Spacecraft", bodyMapForPropagation, transferBodyOrder );

    // Define integrator settings
    std::shared_ptr< numerical_integrators::IntegratorSettings < > > integratorSettings =
            std::make_shared< numerical_integrators::IntegratorSettings < > > (
                numerical_integrators::rungeKutta4, TUDAT_NAN, 1000.0 );

    // Create list of relevant bodies
    std::vector< std::string > bodyList;
    for( unsigned int i = 0; i < transferBodyOrder.size( ); i++ )
    {
        if( std::find( bodyList.begin( ), bodyList.end( ), transferBodyOrder.at( i ) ) ==
                bodyList.end( ) )
        {
            bodyList.push_back( transferBodyOrder.at( i ) );
        }
    }
    bodyList.push_back( "Sun" );

    // Create list of dependent variables to save (distance to all flyby bodies and Sun)
    std::vector< std::shared_ptr< SingleDependentVariableSaveSettings > > dependentVariableList;
    for( unsigned int i = 0; i < bodyList.size( ); i++ )
    {
        dependentVariableList.push_back( std::make_shared< SingleDependentVariableSaveSettings >(
                                             relative_distance_dependent_variable, "Spacecraft", bodyList.at( i ) ) );
    }

    // Save dependent variables for each propagation leg
    std::vector< std::shared_ptr< DependentVariableSaveSettings > > dependentVariablesToSave;
    for( unsigned int j = 0; j < transferBodyOrder.size( ); j++ )
    {
        dependentVariablesToSave.push_back( std::make_shared< DependentVariableSaveSettings >(
                                                dependentVariableList ) );
    }

    // Define propagator type
    TranslationalPropagatorType propagatorType = cowell;

    // Create propagator settings for patched conic (per arc; backward and forward from arc midpoint)
    // Propagation currently terminates on sphere of influence of body.
    std::vector< std::pair< std::shared_ptr< propagators::TranslationalStatePropagatorSettings< double > >,
            std::shared_ptr< propagators::TranslationalStatePropagatorSettings< double > > > > propagatorSettings =
            getPatchedConicPropagatorSettings(
                bodyMapForPropagation, accelerationMap, transferBodyOrder, "Sun", "Spacecraft", transferLegTypes,
                trajectoryIndependentVariables, minimumPericenterRadii, departureCaptureSemiMajorAxes,
                departureCaptureEccentricities, dependentVariablesToSave, propagatorType, true );

    // Create an object of `steady_clock` class
    std::chrono::steady_clock sc;

    // Start timer
    auto start = sc.now();

    // Propagate full dynamics of problem
    std::map< int, std::map< double, Eigen::Vector6d > > lambertTargeterResultForEachLeg;
    std::map< int, std::map< double, Eigen::Vector6d > > fullProblemResultForEachLeg;
    std::map< int, std::map< double, Eigen::VectorXd > > dependentVariableResultForEachLeg;
    fullPropagationPatchedConicsTrajectory(
                bodyMapForPropagation, transferBodyOrder,
                "Sun", transferLegTypes, trajectoryIndependentVariables, minimumPericenterRadii,
                departureCaptureSemiMajorAxes, departureCaptureEccentricities,
                propagatorSettings, integratorSettings,
                lambertTargeterResultForEachLeg, fullProblemResultForEachLeg, dependentVariableResultForEachLeg );

    // End timer (starting & ending is done by measuring the time at the moment the process started & ended respectively)
    auto end = sc.now();

    // Measure time span between start & end
    auto time_span = static_cast<std::chrono::duration<double>>(end - start);
    double runTimeInSeconds = time_span.count( );

    std::cout<<"Operation took: "<<runTimeInSeconds<<" seconds"<<std::endl;

    double currentArcMiddleTime = trajectoryParameters.at( 0 ) + trajectoryParameters.at( 1 ) / 2.0;
    for( auto resultIterator : fullProblemResultForEachLeg )
    {
        int currentArc = resultIterator.first;

        // Retrieve state history for current arc
        std::map< double, Eigen::Vector6d > fullProblemSolution = resultIterator.second;

        // Retrieve numerical state at middle of arc.
        Eigen::Vector6d currentArcMiddleState = interpolators::createOneDimensionalInterpolator(
                    fullProblemSolution, std::make_shared< interpolators::LagrangeInterpolatorSettings >(
                        8 ) )->interpolate( currentArcMiddleTime * 86400.0 );

        // Reset integrator initial time
        integratorSettings->initialTime_ = currentArcMiddleTime * 86400.0;

        // Retrieve propagation settings for forward propagation, and reset initial state/final time
        std::shared_ptr< propagators::TranslationalStatePropagatorSettings< double > > forwardPropagatorSettings =
                propagatorSettings.at( resultIterator.first ).second;
        forwardPropagatorSettings->resetInitialStates( currentArcMiddleState );
        forwardPropagatorSettings->resetTerminationSettings( std::make_shared< PropagationTimeTerminationSettings >(
                                                                 fullProblemSolution.rbegin( )->first ) );

        // Ensure time step is positive (forward integration)
        integratorSettings->initialTimeStep_ = std::fabs( integratorSettings->initialTimeStep_ );

        // Propagate dynamics forward and print results to file
        SingleArcDynamicsSimulator< > forwardDynamicsSimulator(
                    bodyMapForPropagation, integratorSettings, forwardPropagatorSettings );
        input_output::writeDataMapToTextFile(
                    forwardDynamicsSimulator.getEquationsOfMotionNumericalSolution( ), "numericalResultForward" +
                    std::to_string( resultIterator.first ) + ".dat", outputPath );


        // Retrieve propagation settings for backward propagation, and reset initial state/final time
        std::shared_ptr< propagators::TranslationalStatePropagatorSettings< double > > backwardPropagatorSettings =
                propagatorSettings.at( resultIterator.first ).second;
        backwardPropagatorSettings->resetInitialStates( currentArcMiddleState );
        backwardPropagatorSettings->resetTerminationSettings( std::make_shared< PropagationTimeTerminationSettings >(
                                                                  fullProblemSolution.begin( )->first ) );

        // Set negative timestep (backward integration)
        integratorSettings->initialTimeStep_ *= -1.0;

        // Propagate dynamics backward and print results to file
        SingleArcDynamicsSimulator< > backwardDynamicsSimulator(
                    bodyMapForPropagation, integratorSettings, backwardPropagatorSettings );
        input_output::writeDataMapToTextFile(
                    backwardDynamicsSimulator.getEquationsOfMotionNumericalSolution( ), "numericalResultBackward" +
                    std::to_string( resultIterator.first ) + ".dat", outputPath );

        // Update arc middle time for next arc.
        if( currentArc < fullProblemResultForEachLeg.size( ) - 1 )
        {
            currentArcMiddleTime += ( trajectoryParameters.at( currentArc + 1 ) + trajectoryParameters.at( currentArc + 2 ) ) / 2.0;

        }
    }

    // Write patched conic results to file for each leg
    for( auto resultIterator : lambertTargeterResultForEachLeg )
    {
        input_output::writeDataMapToTextFile(
                    resultIterator.second, "lambertResult" +
                    std::to_string( resultIterator.first ) + ".dat", outputPath );
    }

    // Write numerical propagation results to file for each leg
    for( auto resultIterator : fullProblemResultForEachLeg )
    {

        input_output::writeDataMapToTextFile(
                    resultIterator.second, "numericalResult" +
                    std::to_string( resultIterator.first ) + ".dat", outputPath );
    }

    // Write numerical propagation results to file for each leg
    for( auto resultIterator : dependentVariableResultForEachLeg )
    {

        input_output::writeDataMapToTextFile(
                    resultIterator.second, "dependentResult" +
                    std::to_string( resultIterator.first ) + ".dat", outputPath );
    }

    // The exit code EXIT_SUCCESS indicates that the program was successfully executed.
    return EXIT_SUCCESS;
}
