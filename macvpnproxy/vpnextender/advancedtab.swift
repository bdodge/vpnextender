//
//  advancedtab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class advancedtab: NSViewController, NSTextFieldDelegate {

    @IBOutlet weak var extVid: NSTextField!
    @IBOutlet weak var extPid: NSTextField!
    
    var myParent:ViewController? = nil

    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
        
        extVid.delegate = self
        extPid.delegate = self
    }
    
    override func viewWillAppear() {
        if (myParent != nil) {
            extVid.stringValue = String(myParent!.def_vid, radix: 16)
            extPid.stringValue = String(myParent!.def_pid, radix: 16)
       }
    }
    
    func vidpidValue(_ vidpid: String) -> UInt16 {
        if let num = UInt16(vidpid, radix: 16) {
            return num
        }
        return 111
    }
    
    func scrubVidPid(_ portstr: String) -> String {
        var retvidpid:String = ""
        var i:Int
        var maxi:Int = portstr.count
        
        if (maxi > 5) {
            maxi = 5;
        }
        i = 0
        while (i < maxi) {
            let nc = portstr[portstr.index(portstr.startIndex, offsetBy: i)]
            if (nc >= "0" && nc <= "9") {
                retvidpid += String(nc)
            }
            else if (nc >= "a" && nc <= "f") {
                retvidpid += String(nc)
            }
            else if (nc >= "A" && nc <= "F") {
                    retvidpid += String(nc)
            }
            i += 1
        }
        if let numport = UInt16(retvidpid, radix: 16) {
            retvidpid = String(numport, radix: 16)
        }
        return retvidpid
    }
    
    func controlTextDidChange(_ obj: Notification) {
        if let textField = obj.object as? NSTextField {
            let newtext = textField.stringValue;
            let cleantext = scrubVidPid(newtext)
            if (self.extVid.identifier == textField.identifier) {
                if (cleantext != newtext) {
                    extVid.stringValue = cleantext
                }
            }
            if (self.extPid.identifier == textField.identifier) {
                if (cleantext != newtext) {
                    extPid.stringValue = cleantext
                }
            }
        }
    }

    @IBAction func onRestartExtender(_ sender: Any) {
        myParent?.StopExtender();
        vpnx_reboot_extender();
        //myParent?.StartExtender();
    }
    
    @IBAction func onDefaults(_ sender: Any) {
        myParent?.DefaultSettings()
        if (myParent != nil) {
            extVid.stringValue = String(myParent!.def_vid, radix: 16)
            extPid.stringValue = String(myParent!.def_pid, radix: 16)
       }
    }
    
    @IBAction func onApply(_ sender: Any) {
        myParent?.def_vid = vidpidValue(extVid.stringValue)
        myParent?.def_pid = vidpidValue(extPid.stringValue)
        myParent?.StoreSettings()
    }
}
