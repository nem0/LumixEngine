using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Threading;
using System.Net.Sockets;
using System.Net; 

namespace editor_ng.FileServer
{
    class FileServer
    {
        private TcpClient m_client;
        private byte[] m_buffer;


        public FileServerUI ui
        {
            get;
            set;
        }

        private void handleMessage(int read)
        {
            if (read < 5)
            {
                m_client.GetStream().Read(m_buffer, read, 5 - read);
                read = 5;
            }

            
            if (m_buffer[4] == 0)
            {
                int len = BitConverter.ToInt32(m_buffer, 0);

                int read2;
                if (read < len + 4)
                    read2 = m_client.GetStream().Read(m_buffer, read, len + 4 - read);
                int idx = BitConverter.ToInt32(m_buffer, 5);

                string path = System.Text.Encoding.ASCII.GetString(m_buffer, 9, len - 5);
                if (System.IO.File.Exists(path))
                {
                    byte[] content = System.IO.File.ReadAllBytes(path);
                    ui.onSendFile(idx, path, content.Length);
                    m_client.GetStream().Write(BitConverter.GetBytes(content.Length + 5), 0, 4);
                    m_client.GetStream().Write(BitConverter.GetBytes(0), 0, 1);
                    m_client.GetStream().Write(BitConverter.GetBytes(idx), 0, 4);
                    m_client.GetStream().Write(content, 0, content.Length);
                }
                else
                {
                    ui.onFileNotFound(idx, path);
                    m_client.GetStream().Write(BitConverter.GetBytes(-1), 0, 4);
                }
            }
        }

        public void onReceive(IAsyncResult result)
        {
            int read = (result.AsyncState as FileServer).m_client.GetStream().EndRead(result);

            if (read > 0)
            {
                handleMessage(read);
            }
            m_client.GetStream().BeginRead(m_buffer, 0, 1024, new AsyncCallback(onReceive), this);
        }

        public void start()
        {
            m_buffer = new byte[1024];
            IPHostEntry ipHost = Dns.GetHostEntry("");
            IPAddress ip_addr = ipHost.AddressList[0];
            IPEndPoint ip_end_point = new IPEndPoint(ip_addr, 10001);
            System.Threading.Thread.Sleep(1000); // waiting for native part /// TODO remove this hack
            m_client = new TcpClient("127.0.0.1", 10001);
            m_client.GetStream().BeginRead(m_buffer, 0, 1024, new AsyncCallback(onReceive), this);
        }
    }
}
