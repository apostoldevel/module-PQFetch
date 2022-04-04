/*++

Program name:

  Apostol Web Service

Module Name:

  PQFetch.hpp

Notices:

  Module: Postgres Query Fetch

Author:

  Copyright (c) Prepodobny Alen

  mailto: alienufo@inbox.ru
  mailto: ufocomp@gmail.com

--*/

#ifndef APOSTOL_PQ_FETCH_HPP
#define APOSTOL_PQ_FETCH_HPP
//----------------------------------------------------------------------------------------------------------------------

extern "C++" {

namespace Apostol {

    namespace Workers {

        class CPQFetch;
        class CFetchHandler;
        //--------------------------------------------------------------------------------------------------------------

        typedef std::function<void (CFetchHandler *Handler)> COnFetchHandlerEvent;
        //--------------------------------------------------------------------------------------------------------------

        class CFetchHandler: public CPollConnection {
        private:

            CPQFetch *m_pModule;

            bool m_Allow;

            CJSON m_Payload;

            COnFetchHandlerEvent m_Handler;

            int AddToQueue();
            void RemoveFromQueue();

        protected:

            void SetAllow(bool Value) { m_Allow = Value; }

        public:

            CFetchHandler(CPQFetch *AModule, const CString &Data, COnFetchHandlerEvent && Handler);

            ~CFetchHandler() override;

            const CJSON &Payload() const { return m_Payload; }

            bool Allow() const { return m_Allow; };
            void Allow(bool Value) { SetAllow(Value); };

            bool Handler();

            void Close() override;

        };

        //--------------------------------------------------------------------------------------------------------------

        //-- CPQFetch --------------------------------------------------------------------------------------------------

        //--------------------------------------------------------------------------------------------------------------

        typedef CPollManager CQueueManager;
        //--------------------------------------------------------------------------------------------------------------

        class CPQFetch: public CApostolModule {
        private:

            CQueue m_Queue;
            CQueueManager m_QueueManager;

            CDateTime m_CheckDate;

            size_t m_Progress;
            size_t m_MaxQueue;

            void InitListen();
            void CheckListen();

            void UnloadQueue();

            void DeleteHandler(CFetchHandler *AHandler);

            static CJSON ParamsToJson(const CStringList &Params);
            static CJSON HeadersToJson(const CHeaders &Headers);

            void InitMethods() override;

            static void QueryException(CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E);

        protected:

            static void DoError(const Delphi::Exception::Exception &E);

            void DoFetch(CFetchHandler *AHandler);
            void DoDone(CFetchHandler *AHandler, CHTTPReply *Reply);
            void DoFail(CFetchHandler *AHandler, const CString &Message);

            void DoGet(CHTTPServerConnection *AConnection) override;
            void DoPost(CHTTPServerConnection *AConnection);

            void DoPostgresNotify(CPQConnection *AConnection, PGnotify *ANotify) override;

            void DoPostgresQueryExecuted(CPQPollQuery *APollQuery) override;
            void DoPostgresQueryException(CPQPollQuery *APollQuery, const Delphi::Exception::Exception &E) override;

            void DoConnected(CObject *Sender);
            void DoDisconnected(CObject *Sender);

        public:

            explicit CPQFetch(CModuleProcess *AProcess);

            ~CPQFetch() override = default;

            static class CPQFetch *CreateModule(CModuleProcess *AProcess) {
                return new CPQFetch(AProcess);
            }

            void PQGet(CHTTPServerConnection *AConnection, const CString &Path);
            void PQPost(CHTTPServerConnection *AConnection, const CString &Path, const CString &Body);

            void Heartbeat() override;

            bool Enabled() override;

            bool CheckLocation(const CLocation &Location) override;

            void IncProgress() { m_Progress++; }
            void DecProgress() { m_Progress--; }

            int AddToQueue(CFetchHandler *AHandler);
            void InsertToQueue(int Index, CFetchHandler *AHandler);
            void RemoveFromQueue(CFetchHandler *AHandler);

            CQueue &Queue() { return m_Queue; }
            const CQueue &Queue() const { return m_Queue; }

            CPollManager *ptrQueueManager() { return &m_QueueManager; }

            CPollManager &QueueManager() { return m_QueueManager; }
            const CPollManager &QueueManager() const { return m_QueueManager; }

        };
    }
}

using namespace Apostol::Workers;
}
#endif //APOSTOL_PQ_FETCH_HPP
