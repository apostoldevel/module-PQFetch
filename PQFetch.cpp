/*++

Program name:

  Apostol Web Service

Module Name:

  PQFetch.cpp

Notices:

  Module: Postgres Query Fetch

Author:

  Copyright (c) Prepodobny Alen

  mailto: alienufo@inbox.ru
  mailto: ufocomp@gmail.com

--*/

//----------------------------------------------------------------------------------------------------------------------

#include "Core.hpp"
#include "PQFetch.hpp"
//----------------------------------------------------------------------------------------------------------------------

extern "C++" {

namespace Apostol {

    namespace Workers {

        CFetchHandler::CFetchHandler(CPQFetch *AModule, const CString &Data, COnFetchHandlerEvent && Handler):
                CPollConnection(AModule->ptrQueueManager()), m_Allow(true) {

            m_pModule = AModule;
            m_Payload = Data;
            m_Handler = Handler;

            AddToQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        CFetchHandler::~CFetchHandler() {
            RemoveFromQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CFetchHandler::Close() {
            m_Allow = false;
            RemoveFromQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        int CFetchHandler::AddToQueue() {
            return m_pModule->AddToQueue(this);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CFetchHandler::RemoveFromQueue() {
            m_pModule->RemoveFromQueue(this);
        }
        //--------------------------------------------------------------------------------------------------------------

        bool CFetchHandler::Handler() {
            if (m_Allow && m_Handler) {
                m_Handler(this);
                return true;
            }
            return false;
        }

        //--------------------------------------------------------------------------------------------------------------

        //-- CPQFetch -------------------------------------------------------------------------------------------------

        //--------------------------------------------------------------------------------------------------------------

        CPQFetch::CPQFetch(CModuleProcess *AProcess) : CApostolModule(AProcess, "pq fetch", "worker/PQFetch") {
            m_Headers.Add("Authorization");

            m_CheckDate = 0;
            m_Progress = 0;
            m_MaxQueue = Config()->PostgresPollMin();

            CPQFetch::InitMethods();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::InitMethods() {
#if defined(_GLIBCXX_RELEASE) && (_GLIBCXX_RELEASE >= 9)
            m_pMethods->AddObject(_T("GET")    , (CObject *) new CMethodHandler(true , [this](auto && Connection) { DoGet(Connection); }));
            m_pMethods->AddObject(_T("POST")   , (CObject *) new CMethodHandler(true , [this](auto && Connection) { DoPost(Connection); }));
            m_pMethods->AddObject(_T("OPTIONS"), (CObject *) new CMethodHandler(true , [this](auto && Connection) { DoOptions(Connection); }));
            m_pMethods->AddObject(_T("HEAD")   , (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
            m_pMethods->AddObject(_T("PUT")    , (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
            m_pMethods->AddObject(_T("DELETE") , (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
            m_pMethods->AddObject(_T("TRACE")  , (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
            m_pMethods->AddObject(_T("PATCH")  , (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
            m_pMethods->AddObject(_T("CONNECT"), (CObject *) new CMethodHandler(false, [this](auto && Connection) { MethodNotAllowed(Connection); }));
#else
            m_pMethods->AddObject(_T("GET")    , (CObject *) new CMethodHandler(true , std::bind(&CPQFetch::DoGet, this, _1)));
            m_pMethods->AddObject(_T("POST")   , (CObject *) new CMethodHandler(true , std::bind(&CPQFetch::DoPost, this, _1)));
            m_pMethods->AddObject(_T("OPTIONS"), (CObject *) new CMethodHandler(true , std::bind(&CPQFetch::DoOptions, this, _1)));
            m_pMethods->AddObject(_T("HEAD")   , (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
            m_pMethods->AddObject(_T("PUT")    , (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
            m_pMethods->AddObject(_T("DELETE") , (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
            m_pMethods->AddObject(_T("TRACE")  , (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
            m_pMethods->AddObject(_T("PATCH")  , (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
            m_pMethods->AddObject(_T("CONNECT"), (CObject *) new CMethodHandler(false, std::bind(&CPQFetch::MethodNotAllowed, this, _1)));
#endif
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::QueryException(CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) {
            auto pConnection = dynamic_cast<CHTTPServerConnection *> (APollQuery->Binding());
            ReplyError(pConnection, CHTTPReply::internal_server_error, E.what());
        }
        //--------------------------------------------------------------------------------------------------------------

        CJSON CPQFetch::ParamsToJson(const CStringList &Params) {
            CJSON Json;
            for (int i = 0; i < Params.Count(); i++) {
                Json.Object().AddPair(Params.Names(i), Params.Values(i));
            }
            return Json;
        }
        //--------------------------------------------------------------------------------------------------------------

        CJSON CPQFetch::HeadersToJson(const CHeaders &Headers) {
            CJSON Json;
            for (int i = 0; i < Headers.Count(); i++) {
                const auto &caHeader = Headers[i];
                Json.Object().AddPair(caHeader.Name(), caHeader.Value());
            }
            return Json;
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoError(const Delphi::Exception::Exception &E) {
            Log()->Error(APP_LOG_ERR, 0, "[PQFetch] Error: %s", E.what());
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoConnected(CObject *Sender) {
            auto pConnection = dynamic_cast<CHTTPClientConnection *>(Sender);
            if (Assigned(pConnection)) {
                auto pSocket = pConnection->Socket();
                if (pSocket != nullptr) {
                    auto pHandle = pSocket->Binding();
                    if (pHandle != nullptr) {
                        Log()->Notice(_T("[%s:%d] Fetch client connected."), pHandle->PeerIP(), pHandle->PeerPort());
                    }
                }
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoDisconnected(CObject *Sender) {
            auto pConnection = dynamic_cast<CHTTPClientConnection *>(Sender);
            if (Assigned(pConnection)) {
                auto pSocket = pConnection->Socket();
                if (pSocket != nullptr) {
                    auto pHandle = pSocket->Binding();
                    if (pHandle != nullptr) {
                        Log()->Notice(_T("[%s:%d] Fetch client disconnected."), pHandle->PeerIP(), pHandle->PeerPort());
                    }
                } else {
                    Log()->Notice(_T("Fetch client disconnected."));
                }
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoPostgresNotify(CPQConnection *AConnection, PGnotify *ANotify) {
#ifdef _DEBUG
            const auto &connInfo = AConnection->ConnInfo();

            DebugMessage("[NOTIFY] [%d] [postgresql://%s@%s:%s/%s] [PID: %d] [%s] %s\n",
                         AConnection->Socket(), connInfo["user"].c_str(), connInfo["host"].c_str(), connInfo["port"].c_str(),
                         connInfo["dbname"].c_str(), ANotify->be_pid, ANotify->relname, ANotify->extra);
#endif
#if defined(_GLIBCXX_RELEASE) && (_GLIBCXX_RELEASE >= 9)
            new CFetchHandler(this, ANotify->extra, [this](auto &&Handler) { DoFetch(Handler); });
#else
            new CFetchHandler(this, ANotify->extra, std::bind(&CPQFetch::DoFetch, this, _1));
#endif
            UnloadQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoPostgresQueryExecuted(CPQPollQuery *APollQuery) {

            auto pResult = APollQuery->Results(0);

            try {
                if (pResult->ExecStatus() != PGRES_TUPLES_OK)
                    throw Delphi::Exception::EDBError(pResult->GetErrorMessage());

                CString errorMessage;

                auto pConnection = dynamic_cast<CHTTPServerConnection *> (APollQuery->Binding());

                if (pConnection != nullptr && !pConnection->ClosedGracefully()) {
                    auto pRequest = pConnection->Request();
                    auto pReply = pConnection->Reply();

                    CStringList ResultObject;
                    CStringList ResultFormat;

                    ResultObject.Add("true");
                    ResultObject.Add("false");

                    ResultFormat.Add("object");
                    ResultFormat.Add("array");
                    ResultFormat.Add("null");

                    const auto &result_object = pRequest->Params[_T("result_object")];
                    const auto &result_format = pRequest->Params[_T("result_format")];

                    if (!result_object.IsEmpty() && ResultObject.IndexOfName(result_object) == -1) {
                        ReplyError(pConnection, CHTTPReply::bad_request, CString().Format("Invalid result_object: %s", result_object.c_str()));
                        return;
                    }

                    if (!result_format.IsEmpty() && ResultFormat.IndexOfName(result_format) == -1) {
                        ReplyError(pConnection, CHTTPReply::bad_request, CString().Format("Invalid result_format: %s", result_format.c_str()));
                        return;
                    }

                    CHTTPReply::CStatusType status = CHTTPReply::ok;

                    try {
                        PQResultToJson(pResult, pReply->Content, result_format, result_object == "true" ? "result" : CString());
                    } catch (Delphi::Exception::Exception &E) {
                        errorMessage = E.what();
                        status = CHTTPReply::bad_request;
                        Log()->Error(APP_LOG_ERR, 0, "%s", E.what());
                    }

                    if (status == CHTTPReply::ok) {
                        pConnection->SendReply(status, nullptr, true);
                    } else {
                        ReplyError(pConnection, status, errorMessage);
                    }
                }
            } catch (Delphi::Exception::Exception &E) {
                QueryException(APollQuery, E);
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoPostgresQueryException(CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) {
            QueryException(APollQuery, E);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoDone(CFetchHandler *AHandler, CHTTPReply *Reply) {

            auto OnExecuted = [this](CPQPollQuery *APollQuery) {
                auto pHandler = dynamic_cast<CFetchHandler *> (APollQuery->Binding());
                DeleteHandler(pHandler);
            };

            auto OnException = [this](CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) {
                auto pHandler = dynamic_cast<CFetchHandler *> (APollQuery->Binding());
                DeleteHandler(pHandler);
                DoError(E);
            };

            const auto &caPayload = AHandler->Payload();

            const auto &caHeaders = HeadersToJson(Reply->Headers).ToString();
            const auto &caContent = Reply->Content;

            const auto &caRequest = caPayload["id"].AsString();
            const auto &caDone = caPayload["done"];

            CStringList SQL;

            SQL.Add(CString()
                            .MaxFormatSize(256 + caRequest.Size() + caHeaders.Size() + caContent.Size())
                            .Format("SELECT CreateResponse(%s::uuid, %d, %s, %s::jsonb, %s);",
                                    PQQuoteLiteral(caRequest).c_str(),
                                    (int) Reply->Status,
                                    PQQuoteLiteral(Reply->StatusText).c_str(),
                                    PQQuoteLiteral(caHeaders).c_str(),
                                    PQQuoteLiteral(caContent).c_str()
                            ));

            if (!caDone.IsNull()) {
                SQL.Add(CString().Format("SELECT %s(%s);", caDone.AsString().c_str(), PQQuoteLiteral(caRequest).c_str()));
            }

            try {
                ExecSQL(SQL, AHandler, OnExecuted, OnException);
            } catch (Delphi::Exception::Exception &E) {
                DoError(E);
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoFail(CFetchHandler *AHandler, const CString &Message) {

            auto OnExecuted = [this](CPQPollQuery *APollQuery) {
                auto pHandler = dynamic_cast<CFetchHandler *> (APollQuery->Binding());
                DeleteHandler(pHandler);
            };

            auto OnException = [this](CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) {
                auto pHandler = dynamic_cast<CFetchHandler *> (APollQuery->Binding());
                DeleteHandler(pHandler);
                DoError(E);
            };

            const auto &caPayload = AHandler->Payload();
            const auto &caRequest = caPayload["id"].AsString();
            const auto &caFail = caPayload["fail"];

            CStringList SQL;

            SQL.Add(CString()
                            .MaxFormatSize(256 + caRequest.Size() + Message.Size())
                            .Format("UPDATE http.request SET state = 3, error = %s WHERE id = %s;",
                                    PQQuoteLiteral(Message).c_str(),
                                    PQQuoteLiteral(caRequest).c_str()
                            ));

            if (!caFail.IsNull()) {
                SQL.Add(CString().Format("SELECT %s(%s);", caFail.AsString().c_str(), PQQuoteLiteral(caRequest).c_str()));
            }

            try {
                ExecSQL(SQL, AHandler, OnExecuted, OnException);
            } catch (Delphi::Exception::Exception &E) {
                DoError(E);
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoFetch(CFetchHandler *AHandler) {

            auto OnRequest = [AHandler](CHTTPClient *Sender, CHTTPRequest *ARequest) {

                const auto &caPayload = AHandler->Payload();

                const auto &method = caPayload["method"].AsString();

                const auto &caContentType = caPayload["headers"]["Content-Type"];
                const auto &caAuthorization = caPayload["headers"]["Authorization"];
                const auto &caContent = caPayload["content"];

                if (!caContent.IsNull()) {
                    ARequest->Content = caContent.AsString();
                }

                CLocation URI(caPayload["resource"].AsString());

                CHTTPRequest::Prepare(ARequest, method.c_str(), URI.href().c_str(), caContentType.IsNull() ? _T("application/json") : caContentType.AsString().c_str());

                if (!caAuthorization.IsNull()) {
                    ARequest->AddHeader(_T("Authorization"), caAuthorization.AsString());
                }

                DebugRequest(ARequest);
            };
            //----------------------------------------------------------------------------------------------------------

            auto OnExecute = [this, AHandler](CTCPConnection *Sender) {
                auto pConnection = dynamic_cast<CHTTPClientConnection *> (Sender);
                auto pReply = pConnection->Reply();

                DoDone(AHandler, pReply);
                DebugReply(pReply);

                return true;
            };
            //----------------------------------------------------------------------------------------------------------

            auto OnException = [this, AHandler](CTCPConnection *Sender, const Delphi::Exception::Exception &E) {
                auto pConnection = dynamic_cast<CHTTPClientConnection *> (Sender);

                DoFail(AHandler, E.what());
                DebugReply(pConnection->Reply());
                DoError(E);
            };
            //----------------------------------------------------------------------------------------------------------

            const auto &caPayload = AHandler->Payload();

            CLocation URI(caPayload["resource"].AsString());

            auto pClient = GetClient(URI.hostname, URI.port);
#if defined(_GLIBCXX_RELEASE) && (_GLIBCXX_RELEASE >= 9)
            pClient->OnConnected([this](auto &&Sender) { DoConnected(Sender); });
            pClient->OnDisconnected([this](auto &&Sender) { DoDisconnected(Sender); });
#else
            pClient->OnConnected(std::bind(&DoFetch::DoConnected, this, _1));
            pClient->OnDisconnected(std::bind(&DoFetch::DoDisconnected, this, _1));
#endif

            pClient->OnRequest(OnRequest);
            pClient->OnExecute(OnExecute);
            pClient->OnException(OnException);

            pClient->Active(true);

            AHandler->Allow(false);
            IncProgress();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::PQGet(CHTTPServerConnection *AConnection, const CString &Path) {

            auto pRequest = AConnection->Request();

            CStringList SQL;

            const auto &caHeaders = HeadersToJson(pRequest->Headers).ToString();
            const auto &caParams = ParamsToJson(pRequest->Params).ToString();

            SQL.Add(CString()
                            .MaxFormatSize(256 + Path.Size() + caHeaders.Size() + caParams.Size())
                            .Format("SELECT * FROM http.get(%s, %s::jsonb, %s::jsonb);",
                                    PQQuoteLiteral(Path).c_str(),
                                    PQQuoteLiteral(caHeaders).c_str(),
                                    PQQuoteLiteral(caParams).c_str()
                            ));

            try {
                ExecSQL(SQL, AConnection);
            } catch (Delphi::Exception::Exception &E) {
                AConnection->CloseConnection(true);
                ReplyError(AConnection, CHTTPReply::bad_request, E.what());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::PQPost(CHTTPServerConnection *AConnection, const CString &Path, const CString &Body) {

            auto pRequest = AConnection->Request();

            CStringList SQL;

            const auto &caHeaders = HeadersToJson(pRequest->Headers).ToString();
            const auto &caParams = ParamsToJson(pRequest->Params).ToString();
            const auto &caBody = Body.IsEmpty() ? "null" : PQQuoteLiteral(Body);

            SQL.Add(CString()
                            .MaxFormatSize(256 + Path.Size() + caHeaders.Size() + caParams.Size() + caBody.Size())
                            .Format("SELECT * FROM http.post(%s, %s::jsonb, %s::jsonb, %s::jsonb);",
                                    PQQuoteLiteral(Path).c_str(),
                                    PQQuoteLiteral(caHeaders).c_str(),
                                    PQQuoteLiteral(caParams).c_str(),
                                    caBody.c_str()
                            ));

            try {
                ExecSQL(SQL, AConnection);
            } catch (Delphi::Exception::Exception &E) {
                AConnection->CloseConnection(true);
                ReplyError(AConnection, CHTTPReply::bad_request, E.what());
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoGet(CHTTPServerConnection *AConnection) {

            auto pRequest = AConnection->Request();
            auto pReply = AConnection->Reply();

            pReply->ContentType = CHTTPReply::json;

            const auto &path = pRequest->Location.pathname;

            if (path.IsEmpty()) {
                AConnection->SendStockReply(CHTTPReply::not_found);
                return;
            }

            PQGet(AConnection, path);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DoPost(CHTTPServerConnection *AConnection) {

            auto pRequest = AConnection->Request();
            auto pReply = AConnection->Reply();

            pReply->ContentType = CHTTPReply::json;

            const auto &path = pRequest->Location.pathname;

            if (path.IsEmpty()) {
                AConnection->SendStockReply(CHTTPReply::not_found);
                return;
            }

            const auto& caContentType = pRequest->Headers[_T("Content-Type")].Lower();
            const auto bContentJson = (caContentType.Find(_T("application/json")) != CString::npos);

            CJSON Json;
            if (!bContentJson) {
                ContentToJson(pRequest, Json);
            }

            const auto& caBody = bContentJson ? pRequest->Content : Json.ToString();

            PQPost(AConnection, path, caBody);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::InitListen() {

            auto OnExecuted = [this](CPQPollQuery *APollQuery) {
                try {
                    auto pResult = APollQuery->Results(0);

                    if (pResult->ExecStatus() != PGRES_COMMAND_OK) {
                        throw Delphi::Exception::EDBError(pResult->GetErrorMessage());
                    }

                    APollQuery->Connection()->Listener(true);
#if defined(_GLIBCXX_RELEASE) && (_GLIBCXX_RELEASE >= 9)
                    APollQuery->Connection()->OnNotify([this](auto && APollQuery, auto && ANotify) { DoPostgresNotify(APollQuery, ANotify); });
#else
                    APollQuery->Connection()->OnNotify(std::bind(&CReplicationServer::DoPostgresNotify, this, _1, _2));
#endif
                } catch (Delphi::Exception::Exception &E) {
                    DoError(E);
                }
            };

            auto OnException = [this](CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) {
                DoError(E);
            };

            CStringList SQL;

            SQL.Add("LISTEN http;");

            try {
                ExecSQL(SQL, nullptr, OnExecuted, OnException);
            } catch (Delphi::Exception::Exception &E) {
                DoError(E);
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::CheckListen() {
            int index = 0;
            while (index < PQClient().PollManager().Count() && !PQClient().Connections(index)->Listener())
                index++;

            if (index == PQClient().PollManager().Count())
                InitListen();
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::DeleteHandler(CFetchHandler *AHandler) {
            delete AHandler;
            DecProgress();
            UnloadQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        int CPQFetch::AddToQueue(CFetchHandler *AHandler) {
            return m_Queue.AddToQueue(this, AHandler);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::InsertToQueue(int Index, CFetchHandler *AHandler) {
            m_Queue.InsertToQueue(this, Index, AHandler);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::RemoveFromQueue(CFetchHandler *AHandler) {
            m_Queue.RemoveFromQueue(this, AHandler);
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::UnloadQueue() {
            const auto index = m_Queue.IndexOf(this);
            if (index != -1) {
                const auto queue = m_Queue[index];
                for (int i = 0; i < queue->Count(); ++i) {
                    auto pHandler = (CFetchHandler *) queue->Item(i);
                    if (pHandler != nullptr) {
                        pHandler->Handler();
                        if (m_Progress >= m_MaxQueue)
                            break;
                    }
                }
            }
        }
        //--------------------------------------------------------------------------------------------------------------

        void CPQFetch::Heartbeat() {
            CApostolModule::Heartbeat();
            const auto now = Now();
            if ((now >= m_CheckDate)) {
                m_CheckDate = now + (CDateTime) 5 / MinsPerDay; // 5 min
                CheckListen();
            }
            UnloadQueue();
        }
        //--------------------------------------------------------------------------------------------------------------

        bool CPQFetch::Enabled() {
            if (m_ModuleStatus == msUnknown)
                m_ModuleStatus = Config()->IniFile().ReadBool(SectionName(), "enable", true) ? msEnabled : msDisabled;
            return m_ModuleStatus == msEnabled;
        }
        //--------------------------------------------------------------------------------------------------------------

        bool CPQFetch::CheckLocation(const CLocation &Location) {
            return Location.pathname.SubString(0, 5) == _T("/api/");
        }
        //--------------------------------------------------------------------------------------------------------------
    }
}
}