#include <BWAPI/Game.h>
#include <BWAPI/Position.h>
#include <BWAPI/CoordinateType.h>
#include <BWAPI/Color.h>
#include <BWAPI/Unitset.h>
#include <BWAPI/Unit.h>
#include <BWAPI/Region.h>
#include <BWAPI/Filters.h>
#include <BWAPI/Player.h>

#include <BWAPI/UnitSizeType.h>
#include <BWAPI/DamageType.h>
#include <BWAPI/ExplosionType.h>
#include <BWAPI/WeaponType.h>

#include <cstdarg>

// Needed by other compilers.
#include <cstring>


namespace BWAPI
{
  using namespace Filter;



  Game *GameWrapper::operator ->() const
  {
    return &bw;
  };

  GameWrapper &GameWrapper::operator <<(GameWrapper::ostream_manipulator fn)
  {
    // Pass manipulator into the stream
    ss << fn;

    // Flush to Broodwar's printf if we see endl or ends
    if (fn == &std::endl<char, std::char_traits<char>> || fn == &std::ends<char, std::char_traits<char>>)
    {
      this->flush();
    }
    return *this;
  };

  void GameWrapper::flush()
  {
    if (ss.str().empty()) return;

    bw.printf("%s", ss.str().c_str());
    ss.str("");
  };


  //------------------------------------ DAMAGE CALCULATION ------------------------------------------
  int damageRatio[DamageTypes::Enum::MAX][UnitSizeTypes::Enum::MAX] =
  {
  // Ind, Sml, Med, Lrg, Non, Unk
    {  0,   0,   0,   0,   0,   0 }, // Independent
    {  0, 128, 192, 256,   0,   0 }, // Explosive
    {  0, 256, 128,  64,   0,   0 }, // Concussive
    {  0, 256, 256, 256,   0,   0 }, // Normal
    {  0, 256, 256, 256,   0,   0 }, // Ignore_Armor
    {  0,   0,   0,   0,   0,   0 }, // None
    {  0,   0,   0,   0,   0,   0 }  // Unknown
  };
  int getDamageFromImpl(UnitType fromType, UnitType toType, Player fromPlayer, Player toPlayer)
  {
    // Retrieve appropriate weapon
    WeaponType wpn = toType.isFlyer() ? fromType.airWeapon() : fromType.groundWeapon();
    if ( wpn == WeaponTypes::None || wpn == WeaponTypes::Unknown )
      return 0;

    // Get initial weapon damage
    int dmg = fromPlayer ? fromPlayer->damage(wpn) : wpn.damageAmount() * wpn.damageFactor();

    // If we need to calculate using armor
    if ( wpn.damageType() != DamageTypes::Ignore_Armor && toPlayer != nullptr )
      dmg -= std::min(dmg, toPlayer->armor(toType));
    
    return dmg * damageRatio[wpn.damageType()][toType.size()] / 256;
  }
  int Game::getDamageFrom(UnitType fromType, UnitType toType, Player fromPlayer, Player toPlayer) const
  {
    // Get self if toPlayer not provided
    if ( toPlayer == nullptr )
      toPlayer = this->self();

    return getDamageFromImpl(fromType, toType, fromPlayer, toPlayer);
  }
  int Game::getDamageTo(UnitType toType, UnitType fromType, Player toPlayer, Player fromPlayer) const
  {
    // Get self if fromPlayer not provided
    if ( fromPlayer == nullptr )
      fromPlayer = this->self();

    return getDamageFromImpl(fromType, toType, fromPlayer, toPlayer);
  }
  //-------------------------------------- BUILD LOCATION --------------------------------------------
  const int MAX_RANGE = 64;
  class PlacementReserve
  {
  public:
    PlacementReserve(int maxRange) : maxSearch( std::min(std::max(0,maxRange),MAX_RANGE) )
    {
      this->reset();
      this->backup();
    };

    void reset()
    {
      memset(data,0,sizeof(data));
    };

    // Checks if the given x/y value is valid for the Placement position
    static bool isValidPos(int x, int y)
    {
      return x >= 0 && x < MAX_RANGE && y >= 0 && y < MAX_RANGE;
    };
    static bool isValidPos(TilePosition p)
    {
      return isValidPos(p.x, p.y);
    };

    // Sets the value in the placement reserve array
    void setValue(int x, int y, unsigned char value)
    {
      if ( isValidPos(x,y) )
        data[y][x] = value;
    };
    void setValue(TilePosition p, unsigned char value)
    {
      this->setValue(p.x,p.y,value);
    };
    void setRange(int left, int top, int right, int bottom, unsigned char value)
    {
      for ( int y = top; y < bottom; ++y )
        for ( int x = left; x < right; ++x )
          this->setValue(x,y,value);
    };
    void setRange(TilePosition lt, TilePosition rb, unsigned char value)
    {
      this->setRange(lt.x,lt.y,rb.x,rb.y,value);
    };

    // Gets the value from the placement reserve array, 0 if position is invalid
    unsigned char getValue(int x, int y) const
    { 
      if ( isValidPos(x,y) )
        return data[y][x];
      return 0;
    };
    unsigned char getValue(TilePosition p) const
    { 
      return this->getValue(p.x,p.y);
    };

    template <typename T>
    void iterate(const T& proc)
    {
      // Get min/max distances
      int min = MAX_RANGE/2 - maxSearch/2;
      int max = min + maxSearch;
      for ( int y = min; y < max; ++y )
        for ( int x = min; x < max; ++x )
          proc(this, x, y);
    };

    bool hasValidSpace() const
    {
      // Get min/max distances
      int min = MAX_RANGE/2 - maxSearch/2;
      int max = min + maxSearch;
      for ( int y = min; y < max; ++y )
      {
        for ( int x = min; x < max; ++x )
        {
          if ( this->getValue(x,y) == 1 )
            return true;
        }
      }
      return false;
    };

    void backup()
    {
      memcpy(save, data, sizeof(data));
    };
    void restore()
    {
      memcpy(data, save, sizeof(data));
    };
    void restoreIfInvalid(const char * /*const pszDebug*/)
    {
      if ( !hasValidSpace() )
      {
        //Broodwar << pszDebug << std::endl;
        restore();
      }
    };
    int getMaxRange() const
    {
      return this->maxSearch;
    };
  private:
    unsigned char data[MAX_RANGE][MAX_RANGE];
    unsigned char save[MAX_RANGE][MAX_RANGE];
    int maxSearch;
  };

  void AssignBuildableLocations(Game const &game, PlacementReserve &reserve, UnitType type, TilePosition desiredPosition)
  {
    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;
    
    // Reserve space for the addon as well
    bool hasAddon = type.canBuildAddon();
    
    // Assign 1 to all buildable locations
    reserve.iterate( [&](PlacementReserve *pr, int x, int y)
                      { 
                        if ( (!hasAddon || game.canBuildHere(start+TilePosition(x+4,y+1), UnitTypes::Terran_Missile_Turret) ) &&
                          game.canBuildHere(start+TilePosition(x,y), type) )
                        {
                          pr->setValue(x, y, 1);
                        }
                      });
  }

  void RemoveDisconnected(Game const &game, PlacementReserve &reserve, TilePosition desiredPosition)
  {
    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;

    // Assign 0 to all locations that aren't connected
    reserve.iterate( [&](PlacementReserve *pr, int x, int y)
                      { 
                        if ( !game.hasPath(Position(desiredPosition), Position(start + TilePosition(x,y)) ) )
                          pr->setValue(x, y, 0);
                      });
  }

  // @TODO: This interpretation might be incorrect
  void ReserveStructureWithPadding(PlacementReserve &reserve, TilePosition currentPosition, TilePosition sizeExtra, int padding, UnitType type, TilePosition desiredPosition)
  {
    TilePosition paddingSize = sizeExtra + TilePosition(padding,padding)*2;
  
    TilePosition topLeft = currentPosition - type.tileSize() - paddingSize/2 - TilePosition(1,1);
    TilePosition topLeftRelative = topLeft - desiredPosition + TilePosition(MAX_RANGE,MAX_RANGE)/2;
    TilePosition maxSize = topLeftRelative + type.tileSize() + paddingSize + TilePosition(1,1);

    reserve.setRange(topLeftRelative, maxSize, 0);
  }

  /*
  void ReserveUnbuildable(PlacementReserve &reserve, UnitType type, TilePosition desiredPosition)
  {
    if ( type == UnitTypes::Special_Start_Location )
      return;

    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;

    // Exclude locations with unbuildable locations, but restore a backup in case there are no more build locations
    reserve.backup();
    reserve.iterate( [&](PlacementReserve *, int x, int y)
                      { 
                        if ( !Broodwar->isBuildable( start + TilePosition(x,y), type != UnitTypes::Special_Start_Location ) )
                          ReserveStructureWithPadding(reserve, start + TilePosition(x,y), TilePosition(1,1), 1, type, desiredPosition);
                      });

    // Restore original if there is nothing left
    reserve.restoreIfInvalid(__FUNCTION__);
  }*/

  void ReserveGroundHeight(Game const &game, PlacementReserve &reserve, TilePosition desiredPosition)
  {
    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;

    // Exclude locations with a different ground height, but restore a backup in case there are no more build locations
    reserve.backup();
    int targetHeight = game.getGroundHeight(desiredPosition);
    reserve.iterate( [&](PlacementReserve *pr, int x, int y)
                      { 
                        if (game.getGroundHeight( start + TilePosition(x,y) ) != targetHeight )
                          pr->setValue(x, y, 0);
                      });

    // Restore original if there is nothing left
    reserve.restoreIfInvalid(__FUNCTION__);
  }

  void ReserveExistingAddonPlacement(Game const &game, PlacementReserve &reserve, TilePosition desiredPosition)
  {
    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;

    //Exclude addon placement locations
    reserve.backup();
    Unitset myUnits = game.self()->getUnits();
    myUnits.erase_if( !(Exists && CanBuildAddon) );
    for ( auto &u : myUnits )
    {
      TilePosition addonPos = (u->getTilePosition() + TilePosition(4,1)) - start;
      reserve.setRange(addonPos, addonPos+TilePosition(2,2), 0);
    }

    // Restore if this gave us no build locations
    reserve.restoreIfInvalid(__FUNCTION__);
  }

  void ReserveStructure(PlacementReserve &reserve, Unit pUnit, int padding, UnitType type, TilePosition desiredPosition)
  {
    ReserveStructureWithPadding(reserve, TilePosition(pUnit->getPosition()), pUnit->getType().tileSize(), padding, type, desiredPosition);
  }

  void ReserveAllStructures(Game const &game, PlacementReserve &reserve, UnitType type, TilePosition desiredPosition)
  {
    if ( type.isAddon() )
      return;
    reserve.backup();

    // Reserve space around owned resource depots and resource containers
    Unitset myUnits = game.self()->getUnits();
    myUnits.erase_if( !(Exists && (IsCompleted || (ProducesLarva && IsMorphing)) && IsBuilding && (IsResourceDepot || IsRefinery)) );
    for ( auto &u : myUnits)
      ReserveStructure(reserve, u, 2, type, desiredPosition);
    
    // Reserve space around neutral resources
    if ( type != UnitTypes::Terran_Bunker )
    {
      Unitset resources = game.getNeutralUnits();
      resources.erase_if( !(Exists && IsResourceContainer) );
      for ( auto &u : resources)
        ReserveStructure(reserve, u, 2, type, desiredPosition);
    }
    reserve.restoreIfInvalid(__FUNCTION__);
  }

  TilePosition gDirections[] = { 
    TilePosition( 1, 1),
    TilePosition( 0, 1),
    TilePosition(-1, 1),
    TilePosition( 1, 0),
    TilePosition(-1, 0),
    TilePosition( 1,-1),
    TilePosition( 0,-1),
    TilePosition(-1,-1)
  };
  void ReserveDefault(Game const &game, PlacementReserve &reserve, UnitType type, TilePosition desiredPosition)
  {
    reserve.backup();
    auto original = reserve;

    // Reserve some space around some specific units
    for ( auto &it : game.self()->getUnits() )
    {
      if ( !it->exists() )
        continue;

      switch ( it->getType() )
      {
      case UnitTypes::Enum::Terran_Factory:
      case UnitTypes::Enum::Terran_Missile_Turret:
      case UnitTypes::Enum::Protoss_Robotics_Facility:
      case UnitTypes::Enum::Protoss_Gateway:
      case UnitTypes::Enum::Protoss_Photon_Cannon:
      default:
        ReserveStructure(reserve, it, 2, type, desiredPosition);
        break;
      case UnitTypes::Enum::Terran_Barracks:
      case UnitTypes::Enum::Terran_Bunker:
      case UnitTypes::Enum::Zerg_Creep_Colony:
        ReserveStructure(reserve, it, 1, type, desiredPosition);
        break;
      }
    }
    
    switch ( type )
    {
      case UnitTypes::Enum::Terran_Barracks:
      case UnitTypes::Enum::Terran_Factory:
      case UnitTypes::Enum::Terran_Missile_Turret:
      case UnitTypes::Enum::Terran_Bunker:
      case UnitTypes::Enum::Protoss_Robotics_Facility:
      case UnitTypes::Enum::Protoss_Gateway:
      case UnitTypes::Enum::Protoss_Photon_Cannon:
        for ( int y = 0; y < 64; ++y )
        {
          for ( int x = 0; x < 64; ++x )
          {
            for ( int dir = 0; dir < 8; ++dir )
            {
              TilePosition p = TilePosition(x,y) + gDirections[dir];
              if ( !PlacementReserve::isValidPos(p) || original.getValue(p) == 0 )
                reserve.setValue(p, 0);
            }

          }
        }
        break;
    }
    reserve.restoreIfInvalid(__FUNCTION__);
  }

  void ReservePlacement(Game const &game, PlacementReserve &reserve, UnitType type, TilePosition desiredPosition, bool /*creep*/)
  {
    // Reset the array
    reserve.reset();

    AssignBuildableLocations(game, reserve, type, desiredPosition);
    RemoveDisconnected(game, reserve, desiredPosition);
    
    // @TODO: Assign 0 to all locations that have a ground distance > maxRange

    // exclude positions off the map
    TilePosition start = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;
    reserve.iterate( [&](PlacementReserve *pr, int x, int y)
                      { 
                        if ( !game.isValid(start+TilePosition(x,y)) )
                          pr->setValue(x, y, 0);
                      } );

    // Return if can't find a valid space
    if ( !reserve.hasValidSpace() )
      return;
    
    ReserveGroundHeight(game, reserve, desiredPosition);
    //ReserveUnbuildable(reserve, type, desiredPosition); // NOTE: canBuildHere already includes this!

    if ( !type.isResourceDepot() )
    {
      ReserveAllStructures(game, reserve, type, desiredPosition);
      ReserveExistingAddonPlacement(game, reserve, desiredPosition);
    }

    // Unit-specific reservations
    switch ( type )
    {
    case UnitTypes::Enum::Protoss_Pylon:  // @TODO
      //reservePylonPlacement();
      break;
    case UnitTypes::Enum::Terran_Bunker:  // @TODO
      //if ( !GetBunkerPlacement() )
      {
        //reserveTurretPlacement();
      }
      break;
    case UnitTypes::Enum::Terran_Missile_Turret:  // @TODO
    case UnitTypes::Enum::Protoss_Photon_Cannon:
      //reserveTurretPlacement();
      break;
    case UnitTypes::Enum::Zerg_Creep_Colony:  // @TODO
      //if ( creep || !GetBunkerPlacement() )
      {
        //reserveTurretPlacement();
      }
      break;
    default:
      if ( !type.isResourceDepot() )
        ReserveDefault(game, reserve, type, desiredPosition);
      break;
    }
  }

  struct buildTemplate
  {
    int startX, startY, stepX, stepY;
  } buildTemplates[] = // [13 + 1]
  {
    { 32, 0, 0, 1 },
    { 0, 32, 1, 0 },
    { 31, 0, 0, 1 },
    { 0, 31, 1, 0 },
    { 33, 0, 0, 1 },
    { 0, 33, 1, 0 },
    { 30, 0, 0, 1 },
    { 29, 0, 0, 1 },
    { 0, 30, 1, 0 },
    { 28, 0, 0, 1 },
    { 0, 29, 1, 0 },
    { 27, 0, 0, 1 },
    { 0, 28, 1, 0 },
    { -1, 0, 0, 0 } // last
  };
  void reserveTemplateSpacing(PlacementReserve &reserve)
  {
    reserve.backup();

    for ( buildTemplate *t = buildTemplates; t->startX != -1; ++t )
    {
      int x = t->startX, y = t->startY;
      for ( int i = 0; i < 64; ++i )
      {
        reserve.setValue(x, y, 0);
        x += t->stepX;
        y += t->stepY;
      }
    }
    
    reserve.restoreIfInvalid(__FUNCTION__);
  }

  // ----- GET BUILD LOCATION
  // @TODO: If self() is nullptr, this will crash
  TilePosition Game::getBuildLocation(UnitType type, TilePosition desiredPosition, int maxRange, bool creep) const
  {
    this->setLastError(); // Reset last error

    // Make sure the type is compatible
    if ( !type.isBuilding() )
    {
      this->setLastError(Errors::Incompatible_UnitType);
      return TilePositions::Invalid;
    }
    // @TODO: Addon

    // Do type-specific checks
    bool trimPlacement = true;
    Region pTargRegion = nullptr;
    Unit pSpecialUnitTarget = nullptr;
    switch ( type )
    {
    case UnitTypes::Enum::Protoss_Pylon:
      pSpecialUnitTarget = this->getClosestUnit(Position(desiredPosition), IsOwned && !IsPowered);
      if ( pSpecialUnitTarget )
      {
        desiredPosition = TilePosition(pSpecialUnitTarget->getPosition());
        trimPlacement = false;
      }
      break;
    case UnitTypes::Enum::Terran_Command_Center:
    case UnitTypes::Enum::Protoss_Nexus:
    case UnitTypes::Enum::Zerg_Hatchery:
    case UnitTypes::Enum::Special_Start_Location:
      trimPlacement = false;
      break;
    case UnitTypes::Enum::Zerg_Creep_Colony:
    case UnitTypes::Enum::Terran_Bunker:
      //if ( Get bunker placement region )
      //  trimPlacement = false;
      break;
    }
    
    PlacementReserve reserve(maxRange);
    ReservePlacement(*this, reserve, type, desiredPosition, creep);

    if ( trimPlacement )
      reserveTemplateSpacing(reserve);

    TilePosition centerPosition = desiredPosition - TilePosition(MAX_RANGE,MAX_RANGE)/2;
    if ( pTargRegion != nullptr )
      desiredPosition = TilePosition(pTargRegion->getCenter());
    
    // Find the best position
    int bestDistance, fallbackDistance;
    TilePosition bestPosition, fallbackPosition;

    bestDistance = fallbackDistance = 999999;
    bestPosition = fallbackPosition = TilePositions::None;
    for ( int passCount = 0; passCount < (pTargRegion ? 2 : 1); ++passCount )
    {
      for ( int y = 0; y < MAX_RANGE; ++y )
        for ( int x = 0; x < MAX_RANGE; ++x )
        {
          // Ignore if space is reserved
          if ( reserve.getValue(x,y) == 0 )
            continue;

          TilePosition currentPosition(TilePosition(x,y) + centerPosition);
          int currentDistance = desiredPosition.getApproxDistance(currentPosition);//Broodwar->getGroundDistance( desiredPosition, currentPosition );
          if ( currentDistance < bestDistance )
          {
            if ( currentDistance <= maxRange )
            {
              bestDistance = currentDistance;
              bestPosition = currentPosition;
            }
            else if ( currentDistance < fallbackDistance )
            {
              fallbackDistance = currentDistance;
              fallbackPosition = currentPosition;
            }
          }
        }
      // Break pass if position is found
      if ( bestPosition != TilePositions::None )
        break;

      // Break if an alternative position was found
      if ( fallbackPosition != TilePositions::None )
      {
        bestPosition = fallbackPosition;
        break;
      }

      // If we were really targetting a region, and couldn't find a position above
      if ( pTargRegion != nullptr ) // Then fallback to the default build position
        desiredPosition = centerPosition;
    }

    return bestPosition;
  }
  //------------------------------------------ ACTIONS -----------------------------------------------
  bool Game::setMap(const std::string &mapFileName)
  {
    return setMap(mapFileName.c_str());
  }
  void Game::setScreenPosition(BWAPI::Position p)
  {
    setScreenPosition(p.x, p.y);
  }
  void Game::pingMinimap(BWAPI::Position p)
  {
    pingMinimap(p.x, p.y);
  }
  void Game::vSendText(const char *format, va_list arg)
  {
    vSendTextEx(false, format, arg);
  }
  void Game::sendText(const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vSendTextEx(false, format, ap);
    va_end(ap);
  }
  void Game::sendTextEx(bool toAllies, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vSendTextEx(toAllies, format, ap);
    va_end(ap);
  };
  void Game::printf(const char *format, ...) const
  {
    va_list ap;
    va_start(ap,format);
    vPrintf(format, ap);
    va_end(ap);
  };
  //--------------------------------------------- HAS POWER --------------------------------------------------
  bool Game::hasPower(int tileX, int tileY, UnitType unitType) const
  {
    if ( unitType.isValid() && unitType < UnitTypes::None )
      return hasPowerPrecise( tileX*32 + unitType.tileWidth()*16, tileY*32 + unitType.tileHeight()*16, unitType);
    return hasPowerPrecise( tileX*32, tileY*32, UnitTypes::None);
  }
  bool Game::hasPower(TilePosition position, UnitType unitType) const
  {
    return hasPower(position.x, position.y, unitType);
  }
  bool Game::hasPower(int tileX, int tileY, int tileWidth, int tileHeight, UnitType unitType) const
  {
    return hasPowerPrecise( tileX*32 + tileWidth*16, tileY*32 + tileHeight*16, unitType);
  }
  bool Game::hasPower(TilePosition position, int tileWidth, int tileHeight, UnitType unitType) const
  {
    return hasPower(position.x, position.y, tileWidth, tileHeight, unitType);
  }
  bool Game::hasPowerPrecise(Position position, UnitType unitType) const
  {
    return hasPowerPrecise(position.x, position.y, unitType);
  }
  //------------------------------------------ MAP DATA -----------------------------------------------
  bool Game::isWalkable(BWAPI::WalkPosition position) const
  {
    return isWalkable(position.x, position.y);
  }
  int Game::getGroundHeight(TilePosition position) const
  {
    return getGroundHeight(position.x, position.y);
  }
  Unitset Game::getUnitsInRadius(int x, int y, int radius, const UnitFilter &pred) const
  {
    return getUnitsInRectangle(x - radius,
                                     y - radius,
                                     x + radius,
                                     y + radius,
                                     [&](Unit u){ return u->getDistance(Position(x,y)) <= radius && (!pred.isValid() || pred(u)); });
  }
  Unitset Game::getUnitsInRadius(Position center, int radius, const UnitFilter &pred) const
  {
    return getUnitsInRadius(center.x, center.y, radius, pred);
  }
  Unitset Game::getUnitsInRectangle(BWAPI::Position topLeft, BWAPI::Position bottomRight, const UnitFilter &pred) const
  {
    return getUnitsInRectangle(topLeft.x, topLeft.y, bottomRight.x, bottomRight.y, pred);
  }
  Unit Game::getClosestUnit(Position center, const UnitFilter &pred, int radius) const
  {
    return getClosestUnitInRectangle(center,
                                      [&](Unit u){ return u->getDistance(center) <= radius && (!pred.isValid() || pred(u));},
                                      center.x - radius,
                                      center.y - radius,
                                      center.x + radius,
                                      center.y + radius);
  }
  //------------------------------------------ REGIONS -----------------------------------------------
  BWAPI::Region Game::getRegionAt(BWAPI::Position position) const
  {
    return getRegionAt(position.x, position.y);
  }
  bool Game::hasPath(Position source, Position destination) const
  {
    if (isValid(source) && isValid(destination))
    {
      Region rgnA = getRegionAt(source);
      Region rgnB = getRegionAt(destination);
      if (rgnA && rgnB && rgnA->getRegionGroupID() == rgnB->getRegionGroupID())
        return setLastError();
    }
    return setLastError(Errors::Unreachable_Location);
  }
  //------------------------------------------ DRAW TEXT ----------------------------------------------
  void Game::drawText(CoordinateType::Enum ctype, int x, int y, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(ctype, x, y, format, ap);
    va_end(ap);
  }
  void Game::drawTextMap(int x, int y, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Map, x, y, format, ap);
    va_end(ap);
  }
  void Game::drawTextMouse(int x, int y, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Mouse, x, y, format, ap);
    va_end(ap);
  }
  void Game::drawTextScreen(int x, int y, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Screen, x, y, format, ap);
    va_end(ap);
  }
  void Game::drawTextMap(Position p, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Map, p.x, p.y, format, ap);
    va_end(ap);
  }
  void Game::drawTextMouse(Position p, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Mouse, p.x, p.y, format, ap);
    va_end(ap);
  }
  void Game::drawTextScreen(Position p, const char *format, ...)
  {
    va_list ap;
    va_start(ap,format);
    vDrawText(BWAPI::CoordinateType::Screen, p.x, p.y, format, ap);
    va_end(ap);
  }
  //------------------------------------------ DRAW BOX -----------------------------------------------
  void Game::drawBoxMap(int left, int top, int right, int bottom, Color color, bool isSolid)
  {
    drawBox(CoordinateType::Map, left, top, right, bottom, color, isSolid);
  }
  void Game::drawBoxMouse(int left, int top, int right, int bottom, Color color, bool isSolid)
  {
    drawBox(CoordinateType::Mouse, left, top, right, bottom, color, isSolid);
  }
  void Game::drawBoxScreen(int left, int top, int right, int bottom, Color color, bool isSolid)
  {
    drawBox(CoordinateType::Screen, left, top, right, bottom, color, isSolid);
  }
  void Game::drawBoxMap(Position leftTop, Position rightBottom, Color color, bool isSolid)
  {
    drawBoxMap(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y, color, isSolid);
  }
  void Game::drawBoxMouse(Position leftTop, Position rightBottom, Color color, bool isSolid)
  {
    drawBoxMouse(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y, color, isSolid);
  }
  void Game::drawBoxScreen(Position leftTop, Position rightBottom, Color color, bool isSolid)
  {
    drawBoxScreen(leftTop.x, leftTop.y, rightBottom.x, rightBottom.y, color, isSolid);
  }
  //------------------------------------------ DRAW TRIANGLE -----------------------------------------------
  void Game::drawTriangleMap(int ax, int ay, int bx, int by, int cx, int cy, Color color, bool isSolid)
  {
    drawTriangle(CoordinateType::Map, ax, ay, bx, by, cx, cy, color, isSolid);
  }
  void Game::drawTriangleMouse(int ax, int ay, int bx, int by, int cx, int cy, Color color, bool isSolid)
  {
    drawTriangle(CoordinateType::Mouse, ax, ay, bx, by, cx, cy, color, isSolid);
  }
  void Game::drawTriangleScreen(int ax, int ay, int bx, int by, int cx, int cy, Color color, bool isSolid)
  {
    drawTriangle(CoordinateType::Screen, ax, ay, bx, by, cx, cy, color, isSolid);
  }
  void Game::drawTriangleMap(Position a, Position b, Position c, Color color, bool isSolid)
  {
    drawTriangleMap(a.x, a.y, b.x, b.y, c.x, c.y, color, isSolid);
  }
  void Game::drawTriangleMouse(Position a, Position b, Position c, Color color, bool isSolid)
  {
    drawTriangleMouse(a.x, a.y, b.x, b.y, c.x, c.y, color, isSolid);
  }
  void Game::drawTriangleScreen(Position a, Position b, Position c, Color color, bool isSolid)
  {
    drawTriangleScreen(a.x, a.y, b.x, b.y, c.x, c.y, color, isSolid);
  }
  //------------------------------------------ DRAW CIRCLE -----------------------------------------------
  void Game::drawCircleMap(int x, int y, int radius, Color color, bool isSolid)
  {
    drawCircle(CoordinateType::Map, x, y, radius, color, isSolid);
  }
  void Game::drawCircleMouse(int x, int y, int radius, Color color, bool isSolid)
  {
    drawCircle(CoordinateType::Mouse, x, y, radius, color, isSolid);
  }
  void Game::drawCircleScreen(int x, int y, int radius, Color color, bool isSolid)
  {
    drawCircle(CoordinateType::Screen, x, y, radius, color, isSolid);
  }
  void Game::drawCircleMap(Position p, int radius, Color color, bool isSolid)
  {
    drawCircleMap(p.x, p.y, radius, color, isSolid);
  }
  void Game::drawCircleMouse(Position p, int radius, Color color, bool isSolid)
  {
    drawCircleMouse(p.x, p.y, radius, color, isSolid);
  }
  void Game::drawCircleScreen(Position p, int radius, Color color, bool isSolid)
  {
    drawCircleScreen(p.x, p.y, radius, color, isSolid);
  }
  //------------------------------------------ DRAW ELLIPSE -----------------------------------------------
  void Game::drawEllipseMap(int x, int y, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipse(CoordinateType::Map, x, y, xrad, yrad, color, isSolid);
  }
  void Game::drawEllipseMouse(int x, int y, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipse(CoordinateType::Mouse, x, y, xrad, yrad, color, isSolid);
  }
  void Game::drawEllipseScreen(int x, int y, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipse(CoordinateType::Screen, x, y, xrad, yrad, color, isSolid);
  }
  void Game::drawEllipseMap(Position p, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipseMap(p.x, p.y, xrad, yrad, color, isSolid);
  }
  void Game::drawEllipseMouse(Position p, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipseMouse(p.x, p.y, xrad, yrad, color, isSolid);
  }
  void Game::drawEllipseScreen(Position p, int xrad, int yrad, Color color, bool isSolid)
  {
    drawEllipseScreen(p.x, p.y, xrad, yrad, color, isSolid);
  }
  //------------------------------------------ DRAW DOT -----------------------------------------------
  void Game::drawDotMap(int x, int y, Color color)
  {
    drawDot(CoordinateType::Map, x, y, color);
  }
  void Game::drawDotMouse(int x, int y, Color color)
  {
    drawDot(CoordinateType::Mouse, x, y, color);
  }
  void Game::drawDotScreen(int x, int y, Color color)
  {
    drawDot(CoordinateType::Screen, x, y, color);
  }
  void Game::drawDotMap(Position p, Color color)
  {
    drawDotMap(p.x, p.y, color);
  }
  void Game::drawDotMouse(Position p, Color color)
  {
    drawDotMouse(p.x, p.y, color);
  }
  void Game::drawDotScreen(Position p, Color color)
  {
    drawDotScreen(p.x, p.y, color);
  }
  //------------------------------------------ DRAW LINE -----------------------------------------------
  void Game::drawLineMap(int x1, int y1, int x2, int y2, Color color)
  {
    drawLine(CoordinateType::Map, x1, y1, x2, y2, color);
  }
  void Game::drawLineMouse(int x1, int y1, int x2, int y2, Color color)
  {
    drawLine(CoordinateType::Mouse, x1, y1, x2, y2, color);
  }
  void Game::drawLineScreen(int x1, int y1, int x2, int y2, Color color)
  {
    drawLine(CoordinateType::Screen, x1, y1, x2, y2, color);
  }
  void Game::drawLineMap(Position a, Position b, Color color)
  {
    drawLineMap(a.x, a.y, b.x, b.y, color);
  }
  void Game::drawLineMouse(Position a, Position b, Color color)
  {
    drawLineMouse(a.x, a.y, b.x, b.y, color);
  }
  void Game::drawLineScreen(Position a, Position b, Color color)
  {
    drawLineScreen(a.x, a.y, b.x, b.y, color);
  }

}

