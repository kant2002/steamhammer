#pragma once

#include "TacticsOrders.h"
#include "The.h"

using namespace UAlbertaBot;

LurkerState::LurkerState()
    : createFrame(the.now())
    , goFrame(-1)
{
}

LurkerState::LurkerState(const LurkerOrder & o)
  : order(o)
  , createFrame(the.now())
  , goFrame(-1)
{
}

LurkerOrders::LurkerOrders()
    : generalTactic(LurkerTactic::Aggressive)
{
}
