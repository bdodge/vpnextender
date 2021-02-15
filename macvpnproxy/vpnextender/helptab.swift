//
//  localtab.swift
//  vpnextender
//
//  Created by Brian Dodge on 1/31/21.
//

import Cocoa

class helptab: NSViewController, NSTextFieldDelegate {
  
    var myParent:ViewController? = nil

    override func viewDidLoad() {
        super.viewDidLoad()
        myParent = parent! as? ViewController
    }
    
    override func viewWillAppear() {
    }
}
