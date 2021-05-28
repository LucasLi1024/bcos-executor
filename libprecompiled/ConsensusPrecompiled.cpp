/**
 *  Copyright (C) 2021 FISCO BCOS.
 *  SPDX-License-Identifier: Apache-2.0
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * @file ConsensusPrecompiled.cpp
 * @author: kyonRay
 * @date 2021-05-26
 */

#include "ConsensusPrecompiled.h"
#include "Utilities.h"
#include <bcos-framework/libcodec/abi/ContractABICodec.h>
#include <bcos-framework/interfaces/ledger/LedgerTypeDef.h>
#include <bcos-framework/interfaces/protocol/CommonError.h>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>

using namespace bcos;
using namespace bcos::executor;
using namespace bcos::storage;
using namespace bcos::precompiled;
using namespace bcos::ledger;

const char* const CSS_METHOD_ADD_SEALER = "addSealer(string,string)";
const char* const CSS_METHOD_ADD_SER = "addObserver(string)";
const char* const CSS_METHOD_REMOVE = "remove(string)";

ConsensusPrecompiled::ConsensusPrecompiled()
{
    name2Selector[CSS_METHOD_ADD_SEALER] = getFuncSelector(CSS_METHOD_ADD_SEALER);
    name2Selector[CSS_METHOD_ADD_SER] = getFuncSelector(CSS_METHOD_ADD_SER);
    name2Selector[CSS_METHOD_REMOVE] = getFuncSelector(CSS_METHOD_REMOVE);
}

PrecompiledExecResult::Ptr ConsensusPrecompiled::call(
    std::shared_ptr<executor::ExecutiveContext> _context, bytesConstRef _param,
    const std::string& _origin, const std::string&, u256& _remainGas)
{
    // parse function name
    uint32_t func = getParamFunc(_param);
    bytesConstRef data = getParamData(_param);

    bcos::codec::abi::ContractABICodec abi(nullptr);
    auto callResult = m_precompiledExecResultFactory->createPrecompiledResult();
    auto gasPricer = m_precompiledGasFactory->createPrecompiledGas();

    showConsensusTable(_context);

    int result = 0;
    if (func == name2Selector[CSS_METHOD_ADD_SEALER])
    {
        // addSealer(string)
        std::string nodeID;
        // TODO: check weight string
        std::string weight;
        abi.abiOut(data, nodeID, weight);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);

        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            auto table = _context->getTableFactory()->openTable(SYS_CONSENSUS);

            auto newEntry = table->newEntry();
            newEntry->setField(NODE_TYPE, ledger::CONSENSUS_SEALER);
            newEntry->setField(NODE_ENABLE_NUMBER,
                boost::lexical_cast<std::string>(_context->blockInfo().number + 1));
            newEntry->setField(NODE_WEIGHT, weight);

            if(_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
            {
                table->setRow(nodeID, newEntry);
                auto commitResult = _context->getTableFactory()->commit();
                if(!commitResult.second||commitResult.second->errorCode()==0)
                {
                    result = int(commitResult.first);
                    PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled")
                            << LOG_DESC("addSealer successfully insert") << LOG_KV("result", result);
                }
                else
                {
                    PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addSealer commit failed")
                        << LOG_KV("errorCode", commitResult.second->errorCode())
                        << LOG_KV("errorMsg", commitResult.second->errorMessage())
                        << LOG_KV("result", result);
                    // FIXME: add unify error code
                    result = -1;
                }
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                // FIXME: add unify error code
                result = -1;
            }
        }
    }
    else if (func == name2Selector[CSS_METHOD_ADD_SER])
    {
        // addObserver(string)
        std::string nodeID;
        // TODO: check weight string
        std::string weight = "-1";
        abi.abiOut(data, nodeID);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("addObserver func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            auto table = _context->getTableFactory()->openTable(SYS_CONSENSUS);
            auto nodeIdList = table->getPrimaryKeys(nullptr);

            auto newEntry = table->newEntry();
            newEntry->setField(NODE_TYPE, ledger::CONSENSUS_OBSERVER);
            newEntry->setField(NODE_ENABLE_NUMBER,
                               boost::lexical_cast<std::string>(_context->blockInfo().number + 1));
            newEntry->setField(NODE_WEIGHT, weight);
            if(_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
            {
                if (checkIsLastSealer(table, nodeID))
                {
                    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                                           << LOG_DESC("addObserver failed, because last sealer");
                    result = CODE_LAST_SEALER;
                }
                else
                {
                    table->setRow(nodeID, newEntry);
                    auto commitResult = _context->getTableFactory()->commit();
                    if (!commitResult.second ||
                        commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
                    {
                        result = int(commitResult.first);
                        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                                               << LOG_DESC("addObserver successfully insert")
                                               << LOG_KV("result", result);
                    }
                    else
                    {
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled")
                            << LOG_DESC("addObserver commit failed")
                            << LOG_KV("errorCode", commitResult.second->errorCode())
                            << LOG_KV("errorMsg", commitResult.second->errorMessage())
                            << LOG_KV("result", result);
                        // FIXME: add unify error code
                        result = -1;
                    }
                }
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                // FIXME: add unify error code
                result = -1;
            }
        }
    }
    else if (func == name2Selector[CSS_METHOD_REMOVE])
    {
        // remove(string)
        std::string nodeID;
        abi.abiOut(data, nodeID);
        // Uniform lowercase nodeID
        boost::to_lower(nodeID);
        PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove func")
                               << LOG_KV("nodeID", nodeID);
        if (nodeID.size() != 128u)
        {
            PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                                   << LOG_DESC("nodeID length error") << LOG_KV("nodeID", nodeID);
            result = CODE_INVALID_NODEID;
        }
        else
        {
            auto table = _context->getTableFactory()->openTable(ledger::SYS_CONSENSUS);

            if(_context->getTableFactory()->checkAuthority(ledger::SYS_CONSENSUS, _origin))
            {
                if (checkIsLastSealer(table, nodeID))
                {
                    PRECOMPILED_LOG(DEBUG) << LOG_BADGE("ConsensusPrecompiled")
                                           << LOG_DESC("remove failed, because last sealer");
                    result = CODE_LAST_SEALER;
                }
                else
                {
                    table->remove(nodeID);
                    auto commitResult = _context->getTableFactory()->commit();
                    if (!commitResult.second ||
                        commitResult.second->errorCode() == protocol::CommonError::SUCCESS)
                    {
                        result = int(commitResult.first);
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove successfully")
                            << LOG_KV("result", result);
                    }
                    else
                    {
                        PRECOMPILED_LOG(DEBUG)
                            << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("remove commit failed")
                            << LOG_KV("errorCode", commitResult.second->errorCode())
                            << LOG_KV("errorMsg", commitResult.second->errorMessage())
                            << LOG_KV("result", result);
                        // FIXME: add unify error code
                        result = -1;
                    }
                }
            }
            else
            {
                PRECOMPILED_LOG(DEBUG)
                        << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("permission denied");
                // FIXME: add unify error code
                result = -1;
            }
        }
    }
    else
    {
        PRECOMPILED_LOG(ERROR) << LOG_BADGE("ConsensusPrecompiled")
                               << LOG_DESC("call undefined function") << LOG_KV("func", func);
    }
    getErrorCodeOut(callResult->mutableExecResult(), result);
    gasPricer->updateMemUsed(callResult->m_execResult.size());
    _remainGas -= gasPricer->calTotalGas();
    return callResult;
}

void ConsensusPrecompiled::showConsensusTable(std::shared_ptr<executor::ExecutiveContext> _context)
{
    auto table = _context->getTableFactory()->openTable(ledger::SYS_CONSENSUS);
    auto nodeIdList = table->getPrimaryKeys(nullptr);

    std::stringstream s;
    s << "ConsensusPrecompiled show table:\n";
    for (size_t i = 0; i < nodeIdList.size(); ++i)
    {
        auto entry = table->getRow(nodeIdList.at(i));
        std::string nodeID = nodeIdList.at(i);
        std::string type = entry->getField(NODE_TYPE);
        std::string enableNumber = entry->getField(NODE_ENABLE_NUMBER);
        std::string weight = entry->getField(NODE_WEIGHT);
        s << "ConsensusPrecompiled[" << i << "]:" << nodeID << "," << type << "," << enableNumber
          << "," << weight << "\n";
    }
    PRECOMPILED_LOG(TRACE) << LOG_BADGE("ConsensusPrecompiled") << LOG_DESC("showConsensusTable")
                           << LOG_KV("consensusTable", s.str());
}

std::shared_ptr<std::map<std::string, storage::Entry::Ptr>>ConsensusPrecompiled::getRowsByNodeType(
    bcos::storage::TableInterface::Ptr _table, std::string const& _nodeType)
{
    auto result = std::make_shared<std::map<std::string, storage::Entry::Ptr>>();
    auto keys = _table->getPrimaryKeys(nullptr);
    for (auto& key: keys)
    {
        auto entry = _table->getRow(key);
        if (entry->getField(NODE_TYPE) == _nodeType)
        {
            result->insert(std::make_pair(key, entry));
        }
    }
    return result;
}

bool ConsensusPrecompiled::checkIsLastSealer(storage::TableInterface::Ptr _table, std::string const& nodeID)
{
    // Check is last sealer or not.
    auto entryMap = getRowsByNodeType(_table, ledger::CONSENSUS_SEALER);
    if (entryMap->size() == 1u && entryMap->cbegin()->first == nodeID)
    {
        // The nodeID in param is the last sealer, cannot be deleted.
        PRECOMPILED_LOG(WARNING) << LOG_BADGE("ConsensusPrecompiled")
                                 << LOG_DESC(
                                        "ConsensusPrecompiled the nodeID in param is the last "
                                        "sealer, cannot be deleted.")
                                 << LOG_KV("nodeID", nodeID);
        return true;
    }
    return false;
}
