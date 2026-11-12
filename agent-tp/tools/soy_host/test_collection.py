import unittest
from types import SimpleNamespace
from unittest.mock import Mock
from app import App
from telemetry import request_ntcs


class TestCollection(unittest.TestCase):
    def setUp(self):
        self.app=App.__new__(App)
        self.app.disconnecting=False
        self.app.collect_only=True
        self.port=SimpleNamespace(is_open=True,write=Mock(side_effect=lambda payload: len(payload)))
        self.app.ports={'command':self.port}
        self.app.log=Mock();self.app.status=Mock();self.app.release=Mock()
        self.app.root=SimpleNamespace(destroy=Mock(),after=Mock())

    def test_only_read_requests_allowed(self):
        self.app.send(request_ntcs())
        self.port.write.assert_called_once()
        with self.assertRaises(RuntimeError):self.app.send('NB+SOYMODE=0,0\r\n')
        self.assertEqual(self.port.write.call_count,1)

    def test_start_stop_are_inert(self):
        self.app.start();self.app.stop()
        self.port.write.assert_not_called()

    def test_disconnect_and_close_without_control(self):
        self.app.disconnect();self.app.close()
        self.assertEqual(self.app.release.call_count,2)
        self.port.write.assert_not_called()
        self.app.root.after.assert_not_called()
        self.app.root.destroy.assert_called_once()


if __name__=='__main__':unittest.main()
