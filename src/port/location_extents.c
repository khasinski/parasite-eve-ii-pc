/* Extents of the C-defined warp-endpoint and collision-room arrays that the stage 2-5 area directories
 * (src/mapui/map_*) borrow. The directories store bare pointers, so the port records each array's size here for
 * validating development locations (src/port/location_check.c). A pointer missing from this list is reported as an
 * unknown extent, not guessed. Akropolis arrays are read from the disc; their extents come from the asset bindings. */
#include "common.h"
#include "gameplay/direction.h"
#include "gameplay/room.h"
#include "rooms/dryfield_back_street.h"
#include "rooms/dryfield_breezeway.h"
#include "rooms/dryfield_cellar.h"
#include "rooms/dryfield_dilapidated_house.h"
#include "rooms/dryfield_driveway.h"
#include "rooms/dryfield_factory.h"
#include "rooms/dryfield_g_r_kitchen.h"
#include "rooms/dryfield_garage.h"
#include "rooms/dryfield_gas_station.h"
#include "rooms/dryfield_general_store.h"
#include "rooms/dryfield_junk_yard.h"
#include "rooms/dryfield_main_street.h"
#include "rooms/dryfield_motel_balcony.h"
#include "rooms/dryfield_motel_lobby.h"
#include "rooms/dryfield_motel_loft.h"
#include "rooms/dryfield_motel_room_1.h"
#include "rooms/dryfield_motel_room_2.h"
#include "rooms/dryfield_motel_room_3.h"
#include "rooms/dryfield_motel_room_4.h"
#include "rooms/dryfield_motel_room_5.h"
#include "rooms/dryfield_motel_room_6.h"
#include "rooms/dryfield_night_back_street.h"
#include "rooms/dryfield_night_breezeway.h"
#include "rooms/dryfield_night_cellar.h"
#include "rooms/dryfield_night_dilapidated_house.h"
#include "rooms/dryfield_night_driveway.h"
#include "rooms/dryfield_night_factory.h"
#include "rooms/dryfield_night_g_r_kitchen.h"
#include "rooms/dryfield_night_garage.h"
#include "rooms/dryfield_night_gas_station.h"
#include "rooms/dryfield_night_general_store.h"
#include "rooms/dryfield_night_junk_yard.h"
#include "rooms/dryfield_night_main_street.h"
#include "rooms/dryfield_night_motel_balcony.h"
#include "rooms/dryfield_night_motel_lobby.h"
#include "rooms/dryfield_night_motel_loft.h"
#include "rooms/dryfield_night_motel_room_1.h"
#include "rooms/dryfield_night_motel_room_2.h"
#include "rooms/dryfield_night_motel_room_3.h"
#include "rooms/dryfield_night_motel_room_4.h"
#include "rooms/dryfield_night_motel_room_5.h"
#include "rooms/dryfield_night_motel_room_6.h"
#include "rooms/dryfield_night_parking_lot.h"
#include "rooms/dryfield_night_r08.h"
#include "rooms/dryfield_night_saloon_g_r.h"
#include "rooms/dryfield_night_souvenir_shop.h"
#include "rooms/dryfield_night_toilet.h"
#include "rooms/dryfield_night_trailer_coach.h"
#include "rooms/dryfield_night_underpass.h"
#include "rooms/dryfield_night_warehouse.h"
#include "rooms/dryfield_night_water_hole.h"
#include "rooms/dryfield_night_water_tank.h"
#include "rooms/dryfield_night_water_tower.h"
#include "rooms/dryfield_parking_lot.h"
#include "rooms/dryfield_r04.h"
#include "rooms/dryfield_r08.h"
#include "rooms/dryfield_saloon_g_r.h"
#include "rooms/dryfield_souvenir_shop.h"
#include "rooms/dryfield_toilet.h"
#include "rooms/dryfield_trailer_coach.h"
#include "rooms/dryfield_underpass.h"
#include "rooms/dryfield_warehouse.h"
#include "rooms/dryfield_water_hole.h"
#include "rooms/dryfield_water_tank.h"
#include "rooms/dryfield_water_tower.h"
#include "rooms/mine_cavern.h"
#include "rooms/mine_forked_tunnel.h"
#include "rooms/mine_gorge.h"
#include "rooms/mine_mesa.h"
#include "rooms/mine_refuge.h"
#include "rooms/mine_secret_passage.h"
#include "rooms/mine_tunnel.h"
#include "rooms/mine_tunnel_entrance.h"
#include "rooms/neo_ark_altar.h"
#include "rooms/neo_ark_bridge.h"
#include "rooms/neo_ark_eve_access_tunnel.h"
#include "rooms/neo_ark_eve_elevator.h"
#include "rooms/neo_ark_forest_zone.h"
#include "rooms/neo_ark_garden.h"
#include "rooms/neo_ark_island.h"
#include "rooms/neo_ark_north_promenade.h"
#include "rooms/neo_ark_observatory.h"
#include "rooms/neo_ark_pavilion.h"
#include "rooms/neo_ark_power_plant_1.h"
#include "rooms/neo_ark_power_plant_2.h"
#include "rooms/neo_ark_pyramid.h"
#include "rooms/neo_ark_r26.h"
#include "rooms/neo_ark_r31.h"
#include "rooms/neo_ark_savanna_zone.h"
#include "rooms/neo_ark_shrine.h"
#include "rooms/neo_ark_south_promenade.h"
#include "rooms/neo_ark_submarine_gallery.h"
#include "rooms/neo_ark_submarine_tunnel.h"
#include "rooms/neo_ark_substation.h"
#include "rooms/neo_ark_woodland_path.h"
#include "rooms/shelter_1f_airlock.h"
#include "rooms/shelter_1f_bulwark.h"
#include "rooms/shelter_1f_guardroom.h"
#include "rooms/shelter_1f_heliport.h"
#include "rooms/shelter_1f_heliport_s4.h"
#include "rooms/shelter_1f_parking_garage.h"
#include "rooms/shelter_1f_tent.h"
#include "rooms/shelter_1f_vehicular_airlock.h"
#include "rooms/shelter_b1_access_tunnel.h"
#include "rooms/shelter_b1_armory.h"
#include "rooms/shelter_b1_control_room.h"
#include "rooms/shelter_b1_control_room_access_tunnel.h"
#include "rooms/shelter_b1_elevator_hall.h"
#include "rooms/shelter_b1_golem_freezer_1.h"
#include "rooms/shelter_b1_main_corridor.h"
#include "rooms/shelter_b1_north_maintenance_walkway.h"
#include "rooms/shelter_b1_pod_access_tunnel.h"
#include "rooms/shelter_b1_pod_service_gantry.h"
#include "rooms/shelter_b1_sleeping_quarters.h"
#include "rooms/shelter_b1_south_maintenance_walkway.h"
#include "rooms/shelter_b1_sterilization_room.h"
#include "rooms/shelter_b1_storeroom.h"
#include "rooms/shelter_b1_transfer_tunnel.h"
#include "rooms/shelter_b1_underground_parking.h"
#include "rooms/shelter_b2_breeding_room.h"
#include "rooms/shelter_b2_elevator.h"
#include "rooms/shelter_b2_elevator_hall.h"
#include "rooms/shelter_b2_laboratory.h"
#include "rooms/shelter_b2_main_corridor.h"
#include "rooms/shelter_b2_north_maintenance_walkway.h"
#include "rooms/shelter_b2_operating_room.h"
#include "rooms/shelter_b2_pod_access_tunnel.h"
#include "rooms/shelter_b2_pod_bottom.h"
#include "rooms/shelter_b2_septic_tank.h"
#include "rooms/shelter_b2_south_maintenance_walkway.h"
#include "rooms/shelter_b3_dumping_hole.h"
#include "rooms/shelter_b3_elevator_hall.h"
#include "rooms/shelter_b3_garbage_incinerator.h"
#include "rooms/shelter_b3_incinerator_control_room.h"
#include "rooms/shelter_b4_lower_sewer.h"
#include "rooms/shelter_b4_reservoir.h"
#include "rooms/shelter_b4_upper_sewer.h"
#include "rooms/shelter_b4_water_supply.h"
#include "rooms/shelter_b6_corridor.h"
#include "rooms/shelter_b6_growth_room.h"
#include "rooms/shelter_b6_nursery.h"
#include "rooms/shelter_b6_training_room.h"
#include "rooms/shelter_r36.h"
#include "rooms/shelter_r37.h"
#include "rooms/shelter_r47.h"
#include "rooms/shelter_r48.h"
#include "rooms/shelter_r49.h"
#include "port/location_check.h"

#define PE_LOCATION_EXTENT(array) { (const void*)(array), sizeof(array) }

const PeLocationArrayExtent gPeLocationArrayExtents[] = {
    PE_LOCATION_EXTENT(gDryfieldGasStationWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMainStreetWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldGeneralStoreWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldR04WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldBackStreetWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldSouvenirShopWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldWarehouseWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldR08WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldDilapidatedHouseWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom1WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom2WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom3WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom4WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldParkingLotWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldToiletWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelLobbyWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldSaloonGRWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldGRKitchenWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldWaterTowerWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldWaterTankWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldBreezewayWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldFactoryWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldGarageWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldDrivewayWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldJunkYardWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldTrailerCoachWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom5WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelBalconyWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom6WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldMotelLoftWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldWaterHoleWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldCellarWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldUnderpassWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldGasStationRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMainStreetRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldGeneralStoreRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldR04RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldBackStreetRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldSouvenirShopRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldWarehouseRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldR08RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldDilapidatedHouseRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom1RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom2RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom3RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom4RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldParkingLotRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldToiletRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelLobbyRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldSaloonGRRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldGRKitchenRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldWaterTowerRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldWaterTankRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldBreezewayRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldFactoryRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldGarageRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldDrivewayRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldJunkYardRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldTrailerCoachRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom5RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelBalconyRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelRoom6RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldMotelLoftRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldWaterHoleRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldCellarRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldUnderpassRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightGasStationWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMainStreetWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightGeneralStoreWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightBackStreetWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightSouvenirShopWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightWarehouseWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightR08WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightDilapidatedHouseWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom1Warps),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom2WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom3WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom4WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightParkingLotWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightToiletWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelLobbyWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightSaloonGRWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightGRKitchenWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightWaterTowerWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightWaterTankWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightBreezewayWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightFactoryWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightGarageWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightDrivewayWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightJunkYardWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightTrailerCoachWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom5WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelBalconyWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom6WarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightMotelLoftWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightWaterHoleWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightCellarWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightUnderpassWarpEntries),
    PE_LOCATION_EXTENT(gDryfieldNightGasStationRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMainStreetRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightGeneralStoreRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightBackStreetRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightSouvenirShopRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightWarehouseRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightR08RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightDilapidatedHouseRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom1RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom2RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom3RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom4RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightParkingLotRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightToiletRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelLobbyRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightSaloonGRRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightGRKitchenRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightWaterTowerCollisionRooms),
    PE_LOCATION_EXTENT(gDryfieldNightWaterTankCollisionRooms),
    PE_LOCATION_EXTENT(gDryfieldNightBreezewayRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightFactoryRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightGarageRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightDrivewayRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightJunkYardRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightTrailerCoachRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom5RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelBalconyRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelRoom6RoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightMotelLoftRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightWaterHoleRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightCellarRoomCollisionResources),
    PE_LOCATION_EXTENT(gDryfieldNightUnderpassRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineMesaWarpEntries),
    PE_LOCATION_EXTENT(gMineCavernWarpEntries),
    PE_LOCATION_EXTENT(gMineTunnelEntranceWarpEntries),
    PE_LOCATION_EXTENT(gMineTunnelWarpEntries),
    PE_LOCATION_EXTENT(gMineGorgeWarpEntries),
    PE_LOCATION_EXTENT(gMineRefugeWarpEntries),
    PE_LOCATION_EXTENT(gMineForkedTunnelWarpEntries),
    PE_LOCATION_EXTENT(gMineSecretPassageWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1ElevatorHallWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1SouthMaintenanceWalkwayWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1StoreroomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1NorthMaintenanceWalkwayWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1ArmoryWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1SleepingQuartersWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1MainCorridorWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1SterilizationRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1PodAccessTunnelWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1ControlRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1AccessTunnelWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1UndergroundParkingWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1GolemFreezer1WarpEntries),
    PE_LOCATION_EXTENT(gShelterB2PodBottomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1PodServiceGantryWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1TransferTunnelWarpEntries),
    PE_LOCATION_EXTENT(gShelterB1ControlRoomAccessTunnelWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2ElevatorWarps),
    PE_LOCATION_EXTENT(gShelterB2ElevatorHallWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2SouthMaintenanceWalkwayWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2OperatingRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2NorthMaintenanceWalkwayWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2LaboratoryWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2BreedingRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2MainCorridorWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2SepticTankWarpEntries),
    PE_LOCATION_EXTENT(gShelterB2PodAccessTunnelWarpEntries),
    PE_LOCATION_EXTENT(gShelterR36WarpEntries),
    PE_LOCATION_EXTENT(gShelterR37WarpEntries),
    PE_LOCATION_EXTENT(gShelter1fHeliportS4WarpEntries),
    PE_LOCATION_EXTENT(gShelterB3DumpingHoleWarpEntries),
    PE_LOCATION_EXTENT(gShelterB3GarbageIncineratorWarpEntries),
    PE_LOCATION_EXTENT(gShelterB3IncineratorControlRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB3ElevatorHallWarpEntries),
    PE_LOCATION_EXTENT(gShelterB4LowerSewerWarpEntries),
    PE_LOCATION_EXTENT(gShelterB4UpperSewerWarpEntries),
    PE_LOCATION_EXTENT(gShelterB4ReservoirWarpEntries),
    PE_LOCATION_EXTENT(gShelterB4WaterSupplyWarpEntries),
    PE_LOCATION_EXTENT(gShelterR47WarpEntries),
    PE_LOCATION_EXTENT(gShelterR48WarpEntries),
    PE_LOCATION_EXTENT(gShelterR49WarpEntries),
    PE_LOCATION_EXTENT(gMineMesaRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineCavernRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineTunnelEntranceRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineTunnelRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineGorgeRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineRefugeRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineForkedTunnelRoomCollisionResources),
    PE_LOCATION_EXTENT(gMineSecretPassageRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB1ElevatorHallRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB1SouthMaintenanceWalkwayRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB1StoreroomRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB1SterilizationRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB1UndergroundParkingRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB2PodBottomRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB2PodAccessTunnelRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB3DumpingHoleRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB3GarbageIncineratorRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterB4ReservoirRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterR48RoomCollisionResources),
    PE_LOCATION_EXTENT(gShelterR49RoomCollisionResources),
    PE_LOCATION_EXTENT(gShelter1fParkingGarageWarps),
    PE_LOCATION_EXTENT(gShelter1fVehicularAirlockWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fBulwarkWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fHeliportWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fAirlockWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fGuardroomWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkObservatoryWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkEveAccessTunnelWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkEveElevatorWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkNorthPromenadeWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkForestZoneWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkSubmarineTunnelWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkPavilionWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkIslandWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkGardenWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkPowerPlant2WarpEntries),
    PE_LOCATION_EXTENT(gNeoArkPowerPlant1WarpEntries),
    PE_LOCATION_EXTENT(gNeoArkSavannaZoneWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkSouthPromenadeWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkAltarWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkShrineWarpEntries),
    PE_LOCATION_EXTENT(gShelterB6NurseryWarpEntries),
    PE_LOCATION_EXTENT(gShelterB6GrowthRoomWarpEntries),
    PE_LOCATION_EXTENT(gShelterB6CorridorWarpEntries),
    PE_LOCATION_EXTENT(gShelterB6TrainingRoomWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkR26Warps),
    PE_LOCATION_EXTENT(gNeoArkBridgeWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fTentWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkWoodlandPathWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkSubmarineGalleryWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkR31WarpEntries),
    PE_LOCATION_EXTENT(gNeoArkPyramidWarpEntries),
    PE_LOCATION_EXTENT(gNeoArkSubstationWarpEntries),
    PE_LOCATION_EXTENT(gShelter1fParkingGarageCollisionRooms),
    PE_LOCATION_EXTENT(gShelter1fVehicularAirlockCollisionResources),
    PE_LOCATION_EXTENT(gShelter1fBulwarkCollisionResources),
    PE_LOCATION_EXTENT(gShelter1fHeliportCollisionResources),
    PE_LOCATION_EXTENT(gShelter1fAirlockRoomCollisionResources),
    PE_LOCATION_EXTENT(gShelter1fGuardroomRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkObservatoryRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkEveAccessTunnelCollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkEveElevatorCollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkNorthPromenadeRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkForestZoneRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkSubmarineTunnelRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkPavilionRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkIslandRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkGardenCollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkPowerPlant2CollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkPowerPlant1CollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkSavannaZoneRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkSouthPromenadeCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkAltarRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkShrineCollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkR26CollisionRooms),
    PE_LOCATION_EXTENT(gNeoArkPyramidRoomCollisionResources),
    PE_LOCATION_EXTENT(gNeoArkSubstationRoomCollisionResources),
};
const int gPeLocationArrayExtentCount = ARRAY_SIZE(gPeLocationArrayExtents);
