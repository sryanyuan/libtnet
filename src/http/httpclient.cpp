#include "httpclient.h"

#include "ioloop.h"
#include "httprequest.h"
#include "connection.h"
#include "sockutil.h"
#include "log.h"
#include "httpclientconn.h"
#include "address.h"

using namespace std;

namespace tnet
{

    HttpClient::HttpClient(IOLoop* loop, int maxClients)
        : m_loop(loop)
        , m_maxClients(maxClients)
    {
    }
    
    HttpClient::~HttpClient()
    {
        IpConn_t::iterator iter = m_conns.begin();
        while(iter != m_conns.end())
        {
            ConnectionPtr_t conn = iter->second.lock();
            if(conn)
            {
                conn->clearEventCallback();
                conn->shutDown();    
            }
            ++iter;    
        } 

        m_conns.clear();
    }

    void HttpClient::request(const string& url, const ResponseCallback_t& callback, enum http_method method)
    {
        HttpRequest req;
        req.url = url;
        req.method = method;
        
        request(req, callback);    
    }

    void HttpClient::request(const string& url, const Headers_t& headers, const ResponseCallback_t& callback, enum http_method method)
    {
        HttpRequest req;
        req.url = url;
        req.headers = headers;
        req.method = method;  
        
        request(req, callback);  
    }

    void HttpClient::request(const string& url, const Headers_t& headers, const string& body, const ResponseCallback_t& callback, enum http_method method)
    {
        HttpRequest req;
        req.url = url;
        req.headers = headers;
        req.method = method;
        req.body = body;
        
        request(req, callback);    
    }

 
    void HttpClient::pushConn(const WeakConnectionPtr_t& conn)
    {
        ConnectionPtr_t c = conn.lock();
        if(!c)
        {
            return;    
        }

        if(m_conns.size() >= m_maxClients)
        {
            c->shutDown();
            return;
        }

        Address addr(0);
        if(SockUtil::getRemoteAddr(c->getSockFd(), addr) != 0)
        {
            c->shutDown();
            return;    
        }

        c->clearEventCallback();
        m_conns.insert(make_pair(addr.ip(), conn));
    }

    WeakConnectionPtr_t HttpClient::popConn(uint32_t ip)
    {
        IpConn_t::iterator iter = m_conns.find(ip);
        if(iter == m_conns.end())
        {
            return WeakConnectionPtr_t();        
        }
        else
        {
            WeakConnectionPtr_t conn = iter->second;
            m_conns.erase(iter);
            return conn;    
        }
        
    }

    void HttpClient::onResponse(const HttpClientConnPtr_t& conn, const HttpResponse& response, ResponseEvent event, const ResponseCallback_t& callback)
    {
        callback(response);
        
        if(event == Response_Complete)
        {
            ConnectionPtr_t c = conn->lockConn();    
    
            pushConn(c);
        }
    }

    void HttpClient::request(HttpRequest& request, const ResponseCallback_t& callback)
    {
        request.parseUrl();

        Address addr(request.host, request.port);
    
        //now we only support ip
        WeakConnectionPtr_t weakConn = popConn(addr.ip());
        ConnectionPtr_t conn = weakConn.lock();
        if(conn)
        {
            HttpClientConnPtr_t httpConn = std::make_shared<HttpClientConn>(conn, std::bind(&HttpClient::onResponse, this, _1, _2, _3, callback));
            conn->setEventCallback(std::bind(&HttpClientConn::onConnEvent, httpConn, _1, _2, _3));
            conn->send(request.dump());
        }
        else
        {
            int fd = SockUtil::create();
            conn = std::make_shared<Connection>(m_loop, fd); 
            conn->setEventCallback(std::bind(&HttpClient::onConnEvent, this, _1, _2, _3, request.dump(), callback));
            conn->connect(addr);    
        }
    }

    void HttpClient::onConnEvent(const ConnectionPtr_t& conn, ConnEvent event,
                                 const void* context, const string& requestData, const ResponseCallback_t& callback)
    {
        switch(event)
        {
            case Conn_ConnectEvent:
                {
                    string data = std::move(requestData);
                    HttpClientConnPtr_t httpConn = std::make_shared<HttpClientConn>(conn, std::bind(&HttpClient::onResponse, this, _1, _2, _3, callback));
                    conn->setEventCallback(std::bind(&HttpClientConn::onConnEvent, httpConn, _1, _2, _3));
                    conn->send(data);
                }
                break;
            default:
                break;    
        }    
    }

}